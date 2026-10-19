#include "collection_internal.h"
#include "core/memory_budget.h"
#include "protocol/media_source.h"
#include <string.h>

_Static_assert(sizeof(CollectionService) <= COLLECTION_MODEL_BUDGET, "Shared compact model budget");
size_t collection_service_size(void) { return sizeof(CollectionService); }
size_t collection_service_alignment(void) { return alignof(CollectionService); }

CollectionResult collection_service_init(void *storage, size_t bytes, JsonPagePool *pool, CollectionService **output)
{
    if (output == NULL) { return COLLECTION_INVALID; }
    *output = NULL;
    if (storage == NULL || pool == NULL || bytes < sizeof(CollectionService) ||
        (uintptr_t)storage % alignof(CollectionService) != 0) { return COLLECTION_INVALID; }
    CollectionService *s = storage;
    memset(s, 0, sizeof(*s)); s->next = COLLECTION_END; s->limit = 64;
    s->pool = pool;
    for (size_t i = 0; i < 2; ++i) { atomic_init(&s->slots[i].page.json.refcount, 0); }
    *output = s; return COLLECTION_OK;
}

CollectionResult collection_configure(CollectionService *s, uint32_t page_limit, JsonString liked_uri)
{
    if (s == NULL || page_limit == 0 || page_limit > 64 ||
        liked_uri.bytes >= sizeof(s->liked_uri) ||
        (liked_uri.bytes != 0 && (liked_uri.data == NULL || memchr(liked_uri.data, 0, liked_uri.bytes) != NULL))) {
        return COLLECTION_INVALID;
    }
    if (collection_resident_pages(s) != 0) { return COLLECTION_FULL; }
    if (liked_uri.bytes != 0) { memmove(s->liked_uri, liked_uri.data, liked_uri.bytes); }
    s->liked_uri[liked_uri.bytes] = 0; s->liked_uri_bytes = liked_uri.bytes;
    s->limit = page_limit; return COLLECTION_OK;
}

size_t collection_resident_pages(const CollectionService *s)
{
    size_t count = 0;
    if (s != NULL) {
        for (size_t i = 0; i < 2; ++i) {
            if (atomic_load_explicit(&s->slots[i].page.json.refcount, memory_order_acquire) != 0) { ++count; }
        }
    }
    return count;
}

CollectionResult collection_reset(CollectionService *s, uint64_t generation)
{
    if (s == NULL || generation <= s->generation) { return COLLECTION_INVALID; }
    if (s->current != NULL) { page_release(&s->current->json); s->current = NULL; }
    s->generation = generation; s->next = 0;
    return COLLECTION_OK;
}

CollectionResult collection_fetch(CollectionService *s, CollectionKind kind,
    uint32_t cursor, uint64_t generation, CollectionRequest *request)
{
    static const ApiOperation operations[] = {API_HOME, API_HOME_SECTION, API_LIBRARY, API_PLAYLIST};
    if (request == NULL) { return COLLECTION_INVALID; }
    *request = (CollectionRequest){0};
    if (s == NULL || (unsigned)kind > COLLECTION_PLAYLIST || generation == 0) { return COLLECTION_INVALID; }
    if (generation < s->generation) { return COLLECTION_STALE; }
    if (generation > s->generation) {
        if (cursor != 0) { return COLLECTION_ORDER; }
        collection_reset(s, generation);
    }
    if (cursor == COLLECTION_END || cursor != s->next ||
        (s->current != NULL && kind != s->kind)) { return COLLECTION_ORDER; }
    for (size_t i = 0; i < 2; ++i) {
        if (s->slots[i].pending && s->slots[i].page.json.generation == generation) { return COLLECTION_PENDING; }
    }
    if (s->serial == UINT64_MAX) { return COLLECTION_FULL; }
    for (uint32_t i = 0; i < 2; ++i) {
        CollectionSlot *slot = &s->slots[i]; unsigned expected = 0;
        if (!atomic_compare_exchange_strong_explicit(&slot->page.json.refcount, &expected,
            UINT_MAX, memory_order_acq_rel, memory_order_acquire)) { continue; }
        /* All fields except the live atomic can be reset after exclusive reservation. */
        memset((char *)&slot->page + sizeof(JsonPage), 0, sizeof(CollectionPage) - sizeof(JsonPage));
        if (json_page_acquire(s->pool, &slot->page.json, generation) != JSON_OK) {
            json_page_discard(&slot->page.json); return COLLECTION_FULL;
        }
        slot->page.kind = kind; slot->page.offset = cursor; slot->page.next = COLLECTION_END;
        slot->serial = ++s->serial; slot->pending = true; slot->cancelled = false;
        *request = (CollectionRequest){{i, slot->serial}, contract_inspect(operations[kind]),
            kind, generation, cursor, s->limit, slot->page.json.input, JSON_PAGE_BODY_CAP};
        s->kind = kind; return COLLECTION_PENDING;
    }
    return COLLECTION_FULL;
}

static CollectionSlot *ticket_slot(CollectionService *s, CollectionTicket ticket)
{
    if (s == NULL || ticket.slot >= 2) { return NULL; }
    CollectionSlot *slot = &s->slots[ticket.slot];
    return slot->pending && slot->serial == ticket.serial ? slot : NULL;
}

CollectionResult collection_cancel(CollectionService *s, CollectionTicket ticket)
{
    CollectionSlot *slot = ticket_slot(s, ticket);
    if (slot == NULL) { return COLLECTION_STALE; }
    slot->cancelled = true; return COLLECTION_PENDING;
}

CollectionResult collection_status(unsigned status)
{
    if (status == 401) { return COLLECTION_AUTH; }
    if (status == 429) { return COLLECTION_RATE_LIMIT; }
    return status == 200 ? COLLECTION_OK : COLLECTION_ERROR;
}

JsonString collection_string(yyjson_val *object, const char *key)
{
    JsonString string = {0};
    (void)json_view_string(object, key, &string); return string;
}

CollectionResult collection_rows(CollectionPage *page, yyjson_val *array)
{
    if (!yyjson_is_arr(array)) { return COLLECTION_INVALID; }
    if (yyjson_arr_size(array) > COLLECTION_ROW_CAP) { return COLLECTION_TOO_LARGE; }
    page->count = (uint32_t)yyjson_arr_size(array); return COLLECTION_OK;
}

static void duplicates(CollectionPage *page, const CollectionPage *previous)
{
    for (uint32_t i = 0; i < page->count; ++i) {
        CollectionItem *item = &page->items[i];
        collection_compact_id(item);
        if (item->id.bytes == 0) { continue; }
        for (uint32_t j = 0; j < i; ++j) {
            if (json_span_equal(item->id, page->items[j].id)) { item->duplicate = true; }
        }
        if (previous != NULL) {
            for (uint32_t j = 0; j < previous->count; ++j) {
                if (json_span_equal(item->id, previous->items[j].id)) { item->duplicate = true; }
            }
        }
    }
}

void collection_compact_id(CollectionItem *item)
{
    const char *prefix = NULL;
    switch (item->kind) {
        case COLLECTION_TRACK: prefix = "spotify:track:"; break;
        case COLLECTION_ALBUM: prefix = "spotify:album:"; break;
        case COLLECTION_ARTIST: prefix = "spotify:artist:"; break;
        case COLLECTION_PLAYLIST_ITEM: prefix = "spotify:playlist:"; break;
        case COLLECTION_EPISODE: prefix = "spotify:episode:"; break;
        default: return;
    }
    size_t bytes = strlen(prefix);
    if (item->uri.bytes != bytes + 22 || memcmp(item->uri.data, prefix, bytes) != 0) { return; }
    char text[23]; memcpy(text, item->uri.data + bytes, 22); text[22] = 0;
    item->has_compact_id = media_gid_from_base62(text, item->compact_id) == MEDIA_OK;
}

CollectionResult collection_complete(CollectionService *s, CollectionTicket ticket,
    CollectionResult result, unsigned status, size_t bytes, const CollectionPage **output)
{
    if (output == NULL) { return COLLECTION_INVALID; }
    *output = NULL;
    CollectionSlot *slot = ticket_slot(s, ticket);
    if (slot == NULL) { return COLLECTION_STALE; }
    CollectionPage *page = &slot->page;
    if (result != COLLECTION_OK && result != COLLECTION_CANCELLED && result != COLLECTION_AUTH &&
        result != COLLECTION_RATE_LIMIT && result != COLLECTION_ERROR && result != COLLECTION_TOO_LARGE &&
        result != COLLECTION_NO_MEMORY) { result = COLLECTION_INVALID; }
    if (page->json.generation != s->generation) { result = COLLECTION_STALE; }
    else if (slot->cancelled) { result = COLLECTION_CANCELLED; }
    else if (result == COLLECTION_OK) { result = collection_status(status); }
    if (result == COLLECTION_OK) {
        JsonResult parsed = json_page_parse(&page->json, bytes);
        result = parsed == JSON_OK ? COLLECTION_OK : parsed == JSON_TOO_LARGE ?
            COLLECTION_TOO_LARGE : parsed == JSON_NO_MEMORY ? COLLECTION_NO_MEMORY : COLLECTION_INVALID;
    }
    if (result == COLLECTION_OK) {
        yyjson_val *root = yyjson_doc_get_root(page->json.doc);
        if (!yyjson_is_obj(root)) { result = COLLECTION_INVALID; }
        else if (page->kind == COLLECTION_HOME || page->kind == COLLECTION_HOME_SECTION) {
            result = collection_home_parse(page, root);
        } else if (page->kind == COLLECTION_LIBRARY) {
            result = collection_library_parse(page, root, (JsonString){s->liked_uri, s->liked_uri_bytes});
        } else { result = collection_playlist_parse(page, root, s->limit); }
    }
    if ((result == COLLECTION_OK || result == COLLECTION_PARTIAL) && s->current != NULL &&
        !json_span_equal(page->revision, s->current->revision)) { result = COLLECTION_REVISION; }
    slot->pending = false;
    if (result != COLLECTION_OK && result != COLLECTION_PARTIAL) {
        json_page_discard(&page->json); return result;
    }
    duplicates(page, s->current);
    if (s->current != NULL) { page_release(&s->current->json); }
    s->current = page; s->next = page->next;
    atomic_store_explicit(&page->json.refcount, 2, memory_order_release); /* service + delivery */
    *output = page;
    return page->boundary ? COLLECTION_BOUNDARY : result;
}
