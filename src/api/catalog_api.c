#include "catalog_internal.h"
#include "collection_api.h"
#include "core/memory_budget.h"
#include <string.h>

_Static_assert(sizeof(CatalogService) + COLLECTION_MODEL_BUDGET <= MEMORY_BUDGET_MODEL, "Shared compact models");
size_t catalog_service_size(void) { return sizeof(CatalogService); }
size_t catalog_service_alignment(void) { return alignof(CatalogService); }
CatalogResult catalog_service_init(void *storage, size_t bytes, JsonPagePool *pool,
    const CatalogConfig *config, CatalogService **output)
{
    if (output == NULL) { return CATALOG_INVALID; }
    *output = NULL;
    if (storage == NULL || bytes < sizeof(CatalogService) || pool == NULL || config == NULL ||
        (uintptr_t)storage % alignof(CatalogService) != 0 || config->album_limit == 0 ||
        config->album_limit > CATALOG_PAGE_LIMIT || config->debounce_ms > 60000) { return CATALOG_INVALID; }
    char locale[64];
    if (config->locale.bytes == 0) { locale[0] = 0; }
    else if (json_base_string_copy(config->locale, locale, sizeof(locale)) != JSON_OK) { return CATALOG_INVALID; }
    CatalogService *s = storage; memset(s, 0, sizeof(*s));
    s->pool = pool; s->config = *config;
    memcpy(s->locale, locale, config->locale.bytes + 1);
    s->config.locale = (JsonString){s->locale, config->locale.bytes};
    for (size_t i = 0; i < 2; ++i) { atomic_init(&s->slots[i].page.json.refcount, 0); }
    *output = s; return CATALOG_OK;
}

CatalogResult catalog_reset(CatalogService *s, uint64_t generation)
{
    if (s == NULL || generation <= s->generation) { return CATALOG_INVALID; }
    if (s->current != NULL) { page_release(&s->current->json); s->current = NULL; }
    s->generation = generation; s->next = 0; s->has_context = s->cancelled = s->debouncing = false;
    return CATALOG_OK;
}

uint32_t catalog_next_wait_ms(const CatalogService *s, uint64_t now_ms)
{
    if (s == NULL || !s->debouncing || s->cancelled) { return UINT32_MAX; }
    if (now_ms >= s->due_ms) { return 0; }
    uint64_t wait = s->due_ms - now_ms;
    return wait >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)wait;
}

CatalogResult catalog_prepare(CatalogService *s, ApiOperation operation, CatalogFamily family,
    JsonString key, uint32_t selector, uint32_t offset, uint32_t limit,
    uint64_t generation, uint64_t now_ms, CatalogRequest *output)
{
    if (output == NULL) { return CATALOG_INVALID; }
    *output = (CatalogRequest){0};
    if (s == NULL || generation == 0 || limit == 0 || limit > CATALOG_PAGE_LIMIT || key.bytes > CATALOG_KEY_CAP ||
        (key.bytes != 0 && (key.data == NULL || memchr(key.data, 0, key.bytes) != NULL)) ||
        now_ms > UINT64_MAX - s->config.debounce_ms) { return CATALOG_INVALID; }
    if (generation < s->generation) { return CATALOG_STALE; }
    /* A new key may borrow the current page. Copy before reset releases its
     * final reference and ASan poisons that input. */
    char key_copy[CATALOG_KEY_CAP + 1];
    if (key.bytes != 0) { memcpy(key_copy, key.data, key.bytes); }
    key_copy[key.bytes] = 0; key.data = key_copy;
    if (generation > s->generation) {
        if (offset != 0) { return CATALOG_ORDER; }
        catalog_reset(s, generation);
    }
    if (s->cancelled) { return CATALOG_CANCELLED; }
    if (!s->has_context) {
        if (offset != 0) { return CATALOG_ORDER; }
        if (key.bytes != 0) { memcpy(s->key, key.data, key.bytes); }
        s->key[key.bytes] = 0; s->key_bytes = key.bytes;
        s->selector = selector; s->family = family; s->limit = limit; s->has_context = true;
        s->debouncing = family == CATALOG_SEARCH_FAMILY && s->config.debounce_ms != 0;
        s->due_ms = now_ms + s->config.debounce_ms;
    } else if (s->family != family || s->selector != selector || s->limit != limit ||
        !json_span_equal(key, (JsonString){s->key, s->key_bytes})) { return CATALOG_INVALID; }
    if (offset == CATALOG_END || offset != s->next) { return CATALOG_ORDER; }
    if (s->debouncing && now_ms < s->due_ms) { return CATALOG_DEBOUNCING; }
    s->debouncing = false;
    for (size_t i = 0; i < 2; ++i) {
        if (s->slots[i].pending && s->slots[i].page.json.generation == generation) { return CATALOG_PENDING; }
    }
    if (s->serial == UINT64_MAX) { return CATALOG_FULL; }
    for (uint32_t i = 0; i < 2; ++i) {
        CatalogSlot *slot = &s->slots[i]; unsigned expected = 0;
        if (!atomic_compare_exchange_strong(&slot->page.json.refcount, &expected, UINT_MAX)) { continue; }
        memset((char *)&slot->page + sizeof(JsonPage), 0, sizeof(CatalogPage) - sizeof(JsonPage));
        if (json_page_acquire(s->pool, &slot->page.json, generation) != JSON_OK) {
            json_page_discard(&slot->page.json); return CATALOG_FULL;
        }
        memcpy(slot->key, s->key, s->key_bytes + 1);
        slot->page.requested_key = (JsonString){slot->key, s->key_bytes};
        slot->page.operation = operation; slot->page.offset = offset;
        slot->page.limit = limit; slot->page.next = CATALOG_END; slot->selector = selector;
        CatalogResult result = catalog_serialize(s, slot);
        if (result != CATALOG_OK) { json_page_discard(&slot->page.json); return result; }
        slot->serial = ++s->serial; slot->pending = true; slot->cancelled = false;
        *output = (CatalogRequest){{i, slot->serial}, contract_inspect(operation), slot->upload,
            slot->upload_bytes, slot->page.json.input, JSON_PAGE_BODY_CAP, generation};
        return CATALOG_PENDING;
    }
    return CATALOG_FULL;
}

static CatalogSlot *lookup(CatalogService *s, CatalogTicket ticket)
{
    if (s == NULL || ticket.slot >= 2) { return NULL; }
    CatalogSlot *slot = &s->slots[ticket.slot];
    return slot->pending && slot->serial == ticket.serial ? slot : NULL;
}

CatalogResult catalog_cancel(CatalogService *s, CatalogTicket ticket)
{
    CatalogSlot *slot = lookup(s, ticket);
    if (slot == NULL) { return CATALOG_STALE; }
    slot->cancelled = true; return CATALOG_PENDING;
}

CatalogResult catalog_cancel_generation(CatalogService *s, uint64_t generation)
{
    if (s == NULL || generation != s->generation) { return CATALOG_STALE; }
    s->cancelled = true; s->debouncing = false;
    if (s->current != NULL) { page_release(&s->current->json); s->current = NULL; }
    for (size_t i = 0; i < 2; ++i) {
        if (s->slots[i].page.json.generation == generation) { s->slots[i].cancelled = true; }
    }
    return CATALOG_OK;
}

CatalogResult catalog_complete(CatalogService *s, CatalogTicket ticket, CatalogResult result,
    unsigned status, size_t bytes, JsonString executed_hash, const CatalogPage **output)
{
    if (output == NULL) { return CATALOG_INVALID; }
    *output = NULL;
    CatalogSlot *slot = lookup(s, ticket);
    if (slot == NULL) { return CATALOG_STALE; }
    CatalogPage *page = &slot->page;
    if (result != CATALOG_OK && result != CATALOG_CANCELLED && result != CATALOG_ERROR &&
        result != CATALOG_AUTH && result != CATALOG_RATE_LIMIT && result != CATALOG_TOO_LARGE) { result = CATALOG_INVALID; }
    if (page->json.generation != s->generation) { result = CATALOG_STALE; }
    else if (slot->cancelled) { result = CATALOG_CANCELLED; }
    else if (result == CATALOG_OK) {
        const ApiContract *c = contract_inspect(page->operation);
        if (!json_span_equal(executed_hash, (JsonString){c->persisted_sha256, 64})) { result = CATALOG_HASH_MISMATCH; }
        else if (status == 401) { result = CATALOG_AUTH; }
        else if (status == 429) { result = CATALOG_RATE_LIMIT; }
        else if (status != 200) { result = CATALOG_ERROR; }
    }
    if (result == CATALOG_OK) {
        JsonResult parsed = json_page_parse(&page->json, bytes);
        result = parsed == JSON_OK ? CATALOG_OK : parsed == JSON_NO_MEMORY ? CATALOG_NO_MEMORY :
            parsed == JSON_TOO_LARGE ? CATALOG_TOO_LARGE : CATALOG_INVALID;
    }
    if (result == CATALOG_OK) {
        yyjson_val *root = yyjson_doc_get_root(page->json.doc), *errors = KGET(root, "errors");
        if (!yyjson_is_obj(root) || (errors != NULL && !yyjson_is_arr(errors))) { result = CATALOG_INVALID; }
        else {
            page->partial = yyjson_arr_size(errors) != 0;
            if (page->operation == API_ARTIST) { result = catalog_parse_artist(page, slot->selector, s->config.image_width, root); }
            else if (page->operation == API_ALBUM || page->operation == API_ALBUM_TRACKS) {
                result = catalog_parse_album(page, s->config.image_width, root);
            } else { result = catalog_parse_search(page, slot->selector, s->config.image_width, root); }
        }
    }
    slot->pending = false;
    if (result != CATALOG_OK) { json_page_discard(&page->json); return result; }
    if (s->current != NULL) { page_release(&s->current->json); }
    s->current = page; s->next = page->next;
    atomic_store_explicit(&page->json.refcount, 2, memory_order_release);
    *output = page;
    return page->boundary ? CATALOG_BOUNDARY : page->partial ? CATALOG_PARTIAL : CATALOG_OK;
}
