#include "lyrics_internal.h"
#include "core/memory_budget.h"
#include <string.h>

_Static_assert(sizeof(LyricsService) <= LYRICS_MODEL_BUDGET, "Bounded lyrics controls");
size_t lyrics_service_size(void) { return sizeof(LyricsService); }
size_t lyrics_service_alignment(void) { return alignof(LyricsService); }
LyricsResult lyrics_service_init(void *storage, size_t bytes, JsonPagePool *pool,
    const LyricsConfig *config, LyricsService **output)
{
    if (output == NULL) { return LYRICS_INVALID; }
    *output = NULL;
    if (storage == NULL || bytes < sizeof(LyricsService) || pool == NULL || config == NULL ||
        (uintptr_t)storage % alignof(LyricsService) != 0 ||
        (config->fixture_origin != NULL && !lyrics_fixture_origin_valid(config->fixture_origin)) ||
        (config->http != NULL && (config->app_version == NULL || config->app_version[0] == 0 ||
            (config->tokens == NULL && config->fixture_origin == NULL)))) { return LYRICS_INVALID; }
    LyricsService *s = storage; memset(s, 0, sizeof(*s)); s->pool = pool; s->config = *config;
    for (size_t i = 0; i < 2; ++i) { atomic_init(&s->slots[i].page.refcount, 0); }
    *output = s; return LYRICS_OK;
}

bool lyrics_document_retain(const LyricsDocument *doc) { return doc != NULL && page_retain(doc->owner); }
bool lyrics_document_release(const LyricsDocument *doc) { return doc != NULL && page_release(doc->owner); }

LyricsResult lyrics_reset(LyricsService *s, uint64_t generation)
{
    if (s == NULL || generation <= s->generation) { return LYRICS_INVALID; }
    if (s->current != NULL) { lyrics_document_release(s->current); s->current = NULL; }
    s->generation = generation; s->bound = false; return LYRICS_OK;
}

LyricsSlot *lyrics_slot(LyricsService *s, LyricsTicket ticket)
{
    if (s == NULL || ticket.slot >= 2) { return NULL; }
    LyricsSlot *slot = &s->slots[ticket.slot];
    return slot->pending && slot->serial == ticket.serial ? slot : NULL;
}

LyricsResult lyrics_request(LyricsService *s, const LyricsTrack *track, uint64_t generation, LyricsRequest *output)
{
    if (output == NULL) { return LYRICS_INVALID; }
    *output = (LyricsRequest){0};
    if (s == NULL || track == NULL || generation == 0) { return LYRICS_INVALID; }
    if (s->closing) { return LYRICS_CLOSED; }
    if (generation < s->generation) { return LYRICS_STALE; }
    /* Track/artwork may borrow a page released by reset. Build before release. */
    wchar_t url[HTTP_URL_CHARS]; uint8_t id[16];
    LyricsResult result = lyrics_build_url(&s->config, track, id, url);
    if (result != LYRICS_OK) { return result; }
    if (generation > s->generation) { lyrics_reset(s, generation); }
    if (s->bound && memcmp(s->track_id, id, 16) != 0) { return LYRICS_INVALID; }
    if (s->current != NULL) { return LYRICS_INVALID; } /* Explicit new generation for a refresh. */
    for (size_t i = 0; i < 2; ++i) {
        if (s->slots[i].pending && s->slots[i].page.generation == generation) { return LYRICS_PENDING; }
    }
    if (s->serial == UINT64_MAX) { return LYRICS_FULL; }
    for (uint32_t i = 0; i < 2; ++i) {
        LyricsSlot *slot = &s->slots[i]; unsigned expected = 0;
        if (!atomic_compare_exchange_strong(&slot->page.refcount, &expected, UINT_MAX)) { continue; }
        if (json_page_acquire(s->pool, &slot->page, generation) != JSON_OK) {
            json_page_discard(&slot->page); return LYRICS_FULL;
        }
        memcpy(slot->url, url, (wcslen(url) + 1) * sizeof(wchar_t)); memcpy(slot->track_id, id, 16);
        memcpy(s->track_id, id, 16); s->bound = true;
        slot->serial = ++s->serial; slot->pending = true; slot->cancelled = slot->http_active = false;
        token_authorization_clear(&slot->authorization);
        if (s->config.http != NULL) {
            result = lyrics_http_start(s, slot);
            if (result != LYRICS_PENDING) {
                slot->pending = false; token_authorization_clear(&slot->authorization);
                json_page_discard(&slot->page); return result;
            }
        }
        *output = (LyricsRequest){{i, slot->serial}, contract_inspect(API_LYRICS), slot->url,
            slot->page.input, JSON_PAGE_BODY_CAP, generation}; return LYRICS_PENDING;
    }
    return LYRICS_FULL;
}

LyricsResult lyrics_cancel(LyricsService *s, LyricsTicket ticket)
{
    LyricsSlot *slot = lyrics_slot(s, ticket);
    if (slot == NULL) { return LYRICS_STALE; }
    slot->cancelled = true;
    if (slot->http_active) { (void)http_cancel(s->config.http, slot->http_id); }
    return LYRICS_PENDING;
}

LyricsResult lyrics_finish(LyricsService *s, LyricsTicket ticket, LyricsResult result,
    unsigned status, size_t bytes, const LyricsDocument **output)
{
    if (output == NULL) { return LYRICS_INVALID; }
    *output = NULL; LyricsSlot *slot = lyrics_slot(s, ticket);
    if (slot == NULL) { return LYRICS_STALE; }
    if (slot->http_active) { return LYRICS_INVALID; }
    if (result != LYRICS_OK && result != LYRICS_CANCELLED && result != LYRICS_ERROR &&
        result != LYRICS_AUTH && result != LYRICS_RATE_LIMIT && result != LYRICS_TOO_LARGE) { result = LYRICS_INVALID; }
    if (slot->page.generation != s->generation) { result = LYRICS_STALE; }
    else if (slot->cancelled || s->closing) { result = LYRICS_CANCELLED; }
    else if (result == LYRICS_OK) {
        if (status == 401 || status == 403) { result = LYRICS_AUTH; }
        else if (status == 429) { result = LYRICS_RATE_LIMIT; }
        else if (status != 200 && status != 404) { result = LYRICS_ERROR; }
    }
    const LyricsDocument *doc = NULL;
    if (result == LYRICS_OK) { result = lyrics_parse(&slot->page, slot->track_id, bytes, status == 404, &doc); }
    slot->pending = false; token_authorization_clear(&slot->authorization);
    if (result != LYRICS_OK) { json_page_discard(&slot->page); return result; }
    s->current = doc; atomic_store_explicit(&slot->page.refcount, 2, memory_order_release);
    *output = doc; return LYRICS_OK;
}

LyricsResult lyrics_complete(LyricsService *s, LyricsTicket ticket, LyricsResult result,
    unsigned status, size_t bytes, const LyricsDocument **output)
{
    if (s != NULL && s->config.http != NULL) {
        if (output != NULL) { *output = NULL; } return LYRICS_INVALID;
    }
    return lyrics_finish(s, ticket, result, status, bytes, output);
}

LyricsResult lyrics_shutdown(LyricsService *s)
{
    if (s == NULL) { return LYRICS_INVALID; }
    s->closing = true;
    if (s->current != NULL) { lyrics_document_release(s->current); s->current = NULL; }
    bool pending = false;
    for (uint32_t i = 0; i < 2; ++i) {
        if (s->slots[i].pending) { (void)lyrics_cancel(s, (LyricsTicket){i, s->slots[i].serial}); }
        pending |= atomic_load_explicit(&s->slots[i].page.refcount, memory_order_acquire) != 0;
    }
    return pending ? LYRICS_PENDING : LYRICS_OK;
}
