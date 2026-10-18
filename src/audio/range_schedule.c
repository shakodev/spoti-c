#include "range_internal.h"
#include <wchar.h>

bool range_urls_valid(const RangeFetcher *f, const MediaSource *source)
{
    if (source->url_count > MEDIA_URLS) { return false; }
    for (size_t i = 0; i < source->url_count; ++i) {
        const char *url = source->urls[i].url; size_t n = strnlen_s(url, MEDIA_URL_BYTES);
        if (n == 0 || n == MEDIA_URL_BYTES || source->urls[i].refresh_at == CORE_CLOCK_NEVER) { return false; }
        if (strncmp(url, "https://", 8) != 0 &&
            !(f->config.fixture_loopback_http && strncmp(url, "http://127.0.0.1:", 17) == 0)) { return false; }
        for (size_t j = 0; j < n; ++j) { if ((unsigned char)url[j] <= 32 || (unsigned char)url[j] > 126 || url[j] == '#' || url[j] == '\\') { return false; } }
    }
    return true;
}

void range_update_requests(RangeFetcher *f)
{
    uint64_t now = core_clock_now(&f->config.clock);
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
        RangeRequest *r = &f->requests[i]; if (r->id.value == 0 || r->result != RANGE_PENDING) { continue; }
        RangeSourceRecord *s = range_source(f, r->source);
        if (s == NULL || s->cancelled) { r->result = RANGE_CANCELLED; continue; }
        if (s->failure != RANGE_OK) { r->result = s->failure; continue; }
        if (core_deadline_expired(now, r->deadline)) { r->result = RANGE_TIMEOUT; continue; }
        if (s->size_known && r->offset >= s->total) { r->result = RANGE_EOF; continue; }
        if (s->size_known && r->bytes > s->total - r->offset) { r->bytes = (size_t)(s->total - r->offset); }
        bool ready = true; uint64_t at = r->offset - r->offset % RANGE_BLOCK_BYTES, last = r->offset + r->bytes - 1;
        for (;;) {
            RangeBlock *b = range_block(f, r->source, at);
            if (b == NULL) { ready = false; }
            else if (b->failure != RANGE_OK) { r->result = b->failure; ready = false; break; }
            else if (b->state != RANGE_READY && b->state != RANGE_PINNED) { ready = false; }
            if (last - at < RANGE_BLOCK_BYTES) { break; } at += RANGE_BLOCK_BYTES;
        }
        if (ready) { r->result = RANGE_OK; }
    }
}
static RangeBlock *allocate(RangeFetcher *f, RangeRequest *request, uint64_t offset)
{
    RangeBlock *selected = NULL;
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) {
        RangeBlock *b = &f->blocks[i];
        if (b->state == RANGE_EMPTY) { selected = b; break; }
        if (b->references == 0 && b->disk_ticket == 0 && f->active != (int)i && !range_reserved(f, b, false) &&
            (selected == NULL || b->stamp < selected->stamp)) { selected = b; }
    }
    if (selected == NULL) { return NULL; }
    range_clear_block(f, selected); selected->state = RANGE_FETCHING; selected->source = request->source;
    selected->offset = offset; selected->priority = request->priority;
    if (f->stamp != UINT64_MAX) { selected->stamp = ++f->stamp; }
    return selected;
}
static RangeResult fetch(RangeFetcher *f, RangeBlock *b, const RangeRequest *request)
{
    RangeSourceRecord *s = range_source(f, b->source); if (s == NULL || s->cancelled) { return RANGE_CANCELLED; }
    uint8_t *data = f->pool + (size_t)(b - f->blocks) * RANGE_BLOCK_BYTES;
    if (!b->cache_tried && f->config.cache != NULL) {
        CacheRecord record = {.offset = b->offset, .total = s->size_known ? s->total : 0}; memcpy(record.file_id, s->file_id, 20);
        CacheResult result = audio_cache_get(f->config.cache, &record, data, range_cache_complete, b, &b->disk_ticket);
        if (result == CACHE_OK) { b->cache_tried = b->disk_read = true; return RANGE_PENDING; }
        if (result == CACHE_PENDING || result == CACHE_FULL) { return RANGE_PENDING; }
        b->cache_tried = true; ++f->stats.cache_errors;
    }
    if (b->disk_ticket != 0 || f->active >= 0) { return RANGE_PENDING; }
    if (b->attempts >= f->config.max_attempts) { return RANGE_HTTP_ERROR; }
    uint64_t now = core_clock_now(&f->config.clock); size_t url = s->url_count;
    for (size_t i = 0; i < s->url_count; ++i) {
        size_t j = (b->url + i) % s->url_count;
        if (!core_deadline_expired(now, s->urls[j].refresh_at)) { url = j; break; }
    }
    if (url == s->url_count) {
        if (f->config.refresh == NULL) { return RANGE_EXPIRED; }
        MediaSource refreshed = {0}; MediaResult result = f->config.refresh(f->config.context, s->id, &refreshed);
        if (result == MEDIA_PENDING) { return RANGE_PENDING; }
        if (result != MEDIA_OK || !range_urls_valid(f, &refreshed)) { return RANGE_EXPIRED; }
        if (memcmp(refreshed.track.file_id, s->file_id, 20) != 0) { s->failure = RANGE_CHANGED; return RANGE_CHANGED; }
        memcpy(s->urls, refreshed.urls, sizeof(s->urls)); s->url_count = refreshed.url_count;
        url = s->url_count;
        for (size_t i = 0; i < s->url_count; ++i) {
            if (!core_deadline_expired(now, s->urls[i].refresh_at)) { url = i; break; }
        }
        if (url >= s->url_count) { return RANGE_EXPIRED; }
    }
    wchar_t wide[HTTP_URL_CHARS];
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s->urls[url].url, -1, wide, HTTP_URL_CHARS) == 0) { return RANGE_INVALID; }
    HttpRequest description = {.url = wide, .method = HTTP_GET, .range = true, .identity_encoding = true,
        .range_first = b->offset, .range_last = b->offset + RANGE_BLOCK_BYTES - 1,
        .body = data, .body_capacity = RANGE_BLOCK_BYTES, .body_limit = RANGE_BLOCK_BYTES,
        .timeout_ms = core_deadline_wait(now, request->deadline)};
    if (description.timeout_ms == 0) { return RANGE_TIMEOUT; }
    HttpResult result = range_transfer_submit(f, &description, &f->http_id);
    if (result == HTTP_FULL) { return RANGE_PENDING; }
    if (result != HTTP_OK) { return RANGE_HTTP_ERROR; }
    b->url = url; b->priority = request->priority; ++b->attempts; ++f->stats.network_attempts;
    f->active = (int)(b - f->blocks); return RANGE_PENDING;
}
RangeResult range_schedule(RangeFetcher *f)
{
    bool current = false;
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
        if (f->requests[i].id.value != 0 && f->requests[i].result == RANGE_PENDING && f->requests[i].priority == RANGE_CURRENT) { current = true; }
    }
    for (unsigned priority = 0; priority <= RANGE_PREFETCH; ++priority) {
        if (priority == RANGE_PREFETCH && current) { break; }
        for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
            RangeRequest *r = &f->requests[i];
            if (r->id.value == 0 || r->result != RANGE_PENDING || (unsigned)r->priority != priority) { continue; }
            uint64_t at = r->offset - r->offset % RANGE_BLOCK_BYTES, last = r->offset + r->bytes - 1;
            for (;;) {
                RangeBlock *b = range_block(f, r->source, at);
                if (b == NULL) { b = allocate(f, r, at); }
                if (b != NULL && b->state == RANGE_FETCHING && b->failure == RANGE_OK && b->disk_ticket == 0 && f->active != (int)(b - f->blocks)) {
                    RangeResult result = fetch(f, b, r); if (result != RANGE_PENDING) { b->failure = result; }
                }
                if (last - at < RANGE_BLOCK_BYTES) { break; } at += RANGE_BLOCK_BYTES;
            }
        }
    }
    if (f->config.cache != NULL && audio_cache_status(f->config.cache) == CACHE_OK) {
        for (size_t i = 0; i < RANGE_BLOCKS; ++i) {
            RangeBlock *b = &f->blocks[i]; RangeSourceRecord *s = range_source(f, b->source);
            if ((b->state != RANGE_READY && b->state != RANGE_PINNED) || b->cache_stored || b->disk_ticket != 0 || s == NULL || s->cancelled) { continue; }
            CacheRecord record = {.offset = b->offset, .total = b->total, .bytes = b->bytes}; memcpy(record.file_id, s->file_id, 20);
            CacheResult result = audio_cache_put(f->config.cache, &record, f->pool + i * RANGE_BLOCK_BYTES, range_cache_complete, b, &b->disk_ticket);
            if (result == CACHE_OK) { b->disk_write = b->cache_stored = true; }
            else if (result != CACHE_FULL && result != CACHE_PENDING) { b->cache_stored = true; ++f->stats.cache_errors; }
        }
    }
    return RANGE_OK;
}
