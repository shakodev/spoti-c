#include "range_internal.h"

bool range_same(RangeSource a, RangeSource b)
{
    return a.generation == b.generation && a.slot == b.slot && a.reserved == 0 && b.reserved == 0;
}
RangeSourceRecord *range_source(RangeFetcher *f, RangeSource id)
{
    if (id.slot >= RANGE_SOURCES || id.generation == 0 || id.reserved != 0) { return NULL; }
    RangeSourceRecord *s = &f->sources[id.slot]; return s->live && range_same(s->id, id) ? s : NULL;
}
RangeBlock *range_block(RangeFetcher *f, RangeSource source, uint64_t offset)
{
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) {
        RangeBlock *b = &f->blocks[i];
        if (b->state != RANGE_EMPTY && b->offset == offset && range_same(b->source, source)) { return b; }
    }
    return NULL;
}
void range_clear_block(RangeFetcher *f, RangeBlock *b)
{
    if (b->references != 0 || b->disk_ticket != 0 || f->active == (int)(b - f->blocks)) { return; }
    memset(f->pool + (size_t)(b - f->blocks) * RANGE_BLOCK_BYTES, 0, RANGE_BLOCK_BYTES);
    *b = (RangeBlock){.owner = f};
}
bool range_reserved(const RangeFetcher *f, const RangeBlock *b, bool current_only)
{
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
        const RangeRequest *r = &f->requests[i];
        if (r->id.value == 0 || (r->result != RANGE_OK && r->result != RANGE_PENDING) ||
            !range_same(r->source, b->source) || (current_only && r->priority != RANGE_CURRENT)) { continue; }
        uint64_t first = r->offset - r->offset % RANGE_BLOCK_BYTES;
        uint64_t last = r->offset + r->bytes - 1;
        if (b->offset >= first && b->offset <= last) { return true; }
    }
    return false;
}
void range_reap_sources(RangeFetcher *f)
{
    for (size_t i = 0; i < RANGE_SOURCES; ++i) {
        RangeSourceRecord *s = &f->sources[i]; if (!s->live || !s->cancelled) { continue; }
        bool retained = false;
        for (size_t j = 0; j < RANGE_BLOCKS; ++j) {
            RangeBlock *b = &f->blocks[j];
            if (b->state != RANGE_EMPTY && range_same(b->source, s->id)) {
                range_clear_block(f, b); retained |= b->state != RANGE_EMPTY;
            }
        }
        if (!retained) { memset(s, 0, sizeof(*s)); }
    }
}
RangeResult range_fetcher_create(Arena *metadata, Arena *blocks, const RangeConfig *config, RangeFetcher **out)
{
    if (out == NULL) { return RANGE_INVALID; } *out = NULL;
    if (metadata == NULL || blocks == NULL || metadata == blocks || blocks->capacity > MEMORY_BUDGET_ENCRYPTED_BLOCKS ||
        config == NULL || config->http == NULL || config->max_attempts > 8) { return RANGE_INVALID; }
    ArenaMark mark = arena_mark(metadata);
    RangeFetcher *f = arena_alloc(metadata, sizeof(*f), _Alignof(RangeFetcher));
    if (f == NULL) { return RANGE_FULL; }
    uint8_t *pool = arena_alloc(blocks, MEMORY_BUDGET_ENCRYPTED_BLOCKS, 1);
    if (pool == NULL) { (void)arena_rewind(metadata, mark); return RANGE_FULL; }
    memset(f, 0, sizeof(*f)); memset(pool, 0, MEMORY_BUDGET_ENCRYPTED_BLOCKS);
    f->pool = pool; f->config = *config; f->active = -1;
    if (f->config.timeout_ms == 0) { f->config.timeout_ms = 8000; }
    if (f->config.max_attempts == 0) { f->config.max_attempts = 3; }
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) { f->blocks[i].owner = f; }
    *out = f; return RANGE_OK;
}
RangeResult range_source_open(RangeFetcher *f, const MediaSource *source, RangeSource *id)
{
    if (f == NULL || source == NULL || id == NULL || source->url_count > MEDIA_URLS) { return RANGE_INVALID; }
    if (f->busy) { return RANGE_BAD_STATE; } if (f->shutdown) { return RANGE_CLOSED; }
    if (f->generation == UINT64_MAX) { return RANGE_EXHAUSTED; }
    if (!range_urls_valid(f, source)) { return RANGE_INVALID; }
    for (size_t i = 0; i < RANGE_SOURCES; ++i) {
        RangeSourceRecord *s = &f->sources[i]; if (s->live) { continue; }
        memset(s, 0, sizeof(*s)); s->live = true; s->id = (RangeSource){++f->generation, (uint32_t)i, 0};
        memcpy(s->file_id, source->track.file_id, 20); memcpy(s->urls, source->urls, sizeof(s->urls)); s->url_count = source->url_count;
        *id = s->id; return RANGE_OK;
    }
    return RANGE_FULL;
}
RangeResult range_source_size(RangeFetcher *f, RangeSource id, uint64_t *size)
{
    if (f == NULL || size == NULL) { return RANGE_INVALID; }
    RangeSourceRecord *s = range_source(f, id); if (s == NULL || s->cancelled) { return RANGE_STALE; }
    if (s->failure != RANGE_OK) { return s->failure; }
    if (!s->size_known) { return RANGE_PENDING; } *size = s->total; return RANGE_OK;
}
RangeResult range_set_size(RangeSourceRecord *s, uint64_t size)
{
    if (s->size_known && s->total != size) { return RANGE_CHANGED; }
    s->size_known = true; s->total = size; return RANGE_OK;
}
RangeResult range_request(RangeFetcher *f, RangeSource source, uint64_t offset, size_t bytes, RangePriority priority, RangeId *id)
{
    if (f == NULL || id == NULL || bytes == 0 || bytes > UINT64_MAX - offset || priority > RANGE_PREFETCH || (int)priority < 0) { return RANGE_INVALID; }
    if (bytes > MEMORY_BUDGET_ENCRYPTED_BLOCKS || (offset % RANGE_BLOCK_BYTES + bytes + RANGE_BLOCK_BYTES - 1) / RANGE_BLOCK_BYTES > RANGE_BLOCKS) { return RANGE_LIMIT; }
    if (f->busy) { return RANGE_BAD_STATE; } if (f->shutdown) { return RANGE_CLOSED; }
    RangeSourceRecord *s = range_source(f, source); if (s == NULL || s->cancelled) { return RANGE_STALE; }
    if (s->failure != RANGE_OK) { return s->failure; }
    if (s->size_known && offset >= s->total) { return RANGE_EOF; }
    if (f->sequence == UINT64_MAX) { return RANGE_EXHAUSTED; }
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
        RangeRequest *r = &f->requests[i]; if (r->id.value != 0) { continue; }
        *r = (RangeRequest){.id = {++f->sequence}, .source = source, .offset = offset, .bytes = bytes, .priority = priority,
            .deadline = core_deadline_after(core_clock_now(&f->config.clock), f->config.timeout_ms), .result = RANGE_PENDING};
        *id = r->id; f->needs_drive = true;
        if (priority == RANGE_CURRENT && f->active >= 0 && f->blocks[f->active].priority == RANGE_PREFETCH) {
            RangeBlock *b = &f->blocks[f->active];
            if (!range_reserved(f, b, true)) { b->preempted = true; (void)http_cancel(f->config.http, f->http_id); ++f->stats.preemptions; }
        }
        return RANGE_OK;
    }
    return RANGE_FULL;
}
RangeResult range_status(RangeFetcher *f, RangeId id, bool consume)
{
    if (f == NULL || id.value == 0) { return RANGE_INVALID; }
    if (consume && f->busy) { return RANGE_BAD_STATE; }
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
        RangeRequest *r = &f->requests[i]; if (r->id.value != id.value) { continue; }
        RangeResult result = r->result;
        if (consume && result != RANGE_PENDING) { memset(r, 0, sizeof(*r)); f->needs_drive = true; }
        return result;
    }
    return RANGE_STALE;
}
RangeResult range_cancel_generation(RangeFetcher *f, RangeSource id)
{
    if (f == NULL) { return RANGE_INVALID; } if (f->busy) { return RANGE_BAD_STATE; }
    RangeSourceRecord *s = range_source(f, id); if (s == NULL) { return RANGE_STALE; }
    s->cancelled = true;
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
        if (f->requests[i].id.value != 0 && range_same(f->requests[i].source, id)) { f->requests[i].result = RANGE_CANCELLED; }
    }
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) {
        RangeBlock *b = &f->blocks[i]; if (b->state == RANGE_EMPTY || !range_same(b->source, id)) { continue; }
        if (f->active == (int)i) { (void)http_cancel(f->config.http, f->http_id); }
        if (b->disk_ticket != 0) { (void)audio_cache_cancel(f->config.cache, b->disk_ticket); }
        range_clear_block(f, b);
    }
    range_reap_sources(f); return RANGE_OK;
}
RangeStats range_stats(const RangeFetcher *f)
{
    RangeStats stats = {0}; if (f == NULL) { return stats; } stats = f->stats; stats.block_pool_bytes = MEMORY_BUDGET_ENCRYPTED_BLOCKS;
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) { ++stats.states[f->blocks[i].state]; }
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) { stats.pending += f->requests[i].id.value != 0 && f->requests[i].result == RANGE_PENDING ? 1u : 0u; }
    for (size_t i = 0; i < RANGE_LEASES; ++i) { stats.leases += f->leases[i].value != 0 ? 1u : 0u; }
    return stats;
}
