#include "range_internal.h"
#include <wchar.h>

void range_cache_complete(void *context, uint64_t ticket, CacheResult result, const CacheRecord *record)
{
    RangeBlock *b = context; RangeFetcher *f = b->owner;
    if (b->disk_ticket != ticket) { return; } b->disk_ticket = 0;
    RangeSourceRecord *s = range_source(f, b->source);
    bool read = b->disk_read; b->disk_read = b->disk_write = false;
    if (s == NULL || s->cancelled || f->shutdown) { range_clear_block(f, b); return; }
    if (!read) { if (result != CACHE_OK && result != CACHE_CANCELLED) { ++f->stats.cache_errors; } return; }
    if (result == CACHE_OK) {
        RangeResult size = range_set_size(s, record->total);
        if (record->offset != b->offset || record->bytes == 0 || record->bytes > RANGE_BLOCK_BYTES ||
            memcmp(s->file_id, record->file_id, 20) != 0) { size = RANGE_CHANGED; }
        if (size != RANGE_OK) { b->failure = size; s->failure = size; }
        else { b->bytes = record->bytes; b->total = record->total; b->state = RANGE_READY; b->cache_stored = true; ++f->stats.cache_hits; }
    } else if (result != CACHE_MISS) { ++f->stats.cache_errors; }
    f->needs_drive = true;
}

RangeResult range_http_poll(RangeFetcher *f)
{
    if (http_pump(f->config.http) != HTTP_OK) { return RANGE_HTTP_ERROR; }
    if (f->active < 0) { return RANGE_OK; }
    HttpCompletion completion; HttpResult consumed = http_consume(f->config.http, f->http_id, &completion);
    if (consumed == HTTP_PENDING) { return RANGE_OK; }
    RangeBlock *b = &f->blocks[f->active]; f->active = -1;
    RangeSourceRecord *s = range_source(f, b->source);
    if (s == NULL || s->cancelled || f->shutdown || b->preempted) { range_clear_block(f, b); f->needs_drive = true; return RANGE_OK; }
    if (consumed != HTTP_OK || !completion.final) { b->failure = RANGE_HTTP_ERROR; return RANGE_OK; }
    uint64_t total = 0; size_t bytes = 0; wchar_t etag[128] = {0};
    RangeResult result = range_parse_response(&completion, b->offset, b->offset + RANGE_BLOCK_BYTES - 1, &total, &bytes, etag);
    if (result == RANGE_OK || result == RANGE_EOF) {
        RangeResult checked = range_set_size(s, total);
        if (checked != RANGE_OK || (s->etag[0] != 0 && wcscmp(s->etag, etag) != 0)) { result = s->failure = RANGE_CHANGED; }
        else if (etag[0] != 0) { wcscpy_s(s->etag, 128, etag); }
    }
    if (result == RANGE_OK) {
        b->bytes = bytes; b->total = total; b->state = RANGE_READY;
        return RANGE_OK;
    }
    if (result == RANGE_EOF) { b->failure = RANGE_EOF; return RANGE_OK; }
    /* Only transient transport/server and signed-URL failures retry. Header,
     * representation, ignored-range and truncation errors remain explicit. */
    bool retryable = result == RANGE_EXPIRED || (result == RANGE_HTTP_ERROR &&
        (completion.status == 0 || completion.status == 408 || completion.status == 429 || completion.status >= 500));
    if (retryable && b->attempts < f->config.max_attempts) {
        if (result == RANGE_EXPIRED && b->url < s->url_count) { s->urls[b->url].refresh_at = 0; }
        ++b->url; f->needs_drive = true;
    } else { b->failure = result; }
    return RANGE_OK;
}
