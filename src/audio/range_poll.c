#include "range_internal.h"

RangeResult range_poll(RangeFetcher *f)
{
    if (f == NULL) { return RANGE_INVALID; } if (f->busy) { return RANGE_BAD_STATE; }
    f->busy = true; f->needs_drive = false;
    if (f->config.cache != NULL) { (void)audio_cache_poll(f->config.cache); }
    RangeResult result = range_http_poll(f); range_reap_sources(f); range_update_requests(f);
    if (!f->shutdown) { (void)range_schedule(f); range_update_requests(f); }
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) {
        RangeBlock *b = &f->blocks[i];
        if (b->state == RANGE_FETCHING && !range_reserved(f, b, false)) {
            if (f->active == (int)i) { b->preempted = true; (void)range_transfer_cancel(f, f->http_id); }
            if (b->disk_ticket != 0) { (void)audio_cache_cancel(f->config.cache, b->disk_ticket); }
            range_clear_block(f, b);
        }
    }
    f->needs_drive = false; f->busy = false; return result;
}
size_t range_wait_handles(const RangeFetcher *f, HANDLE handles[3])
{
    if (f == NULL || handles == NULL) { return 0; }
    size_t count = 0; HANDLE wake = range_transfer_event(f);
    if (wake != NULL) { handles[count++] = wake; }
    if (f->config.cache != NULL) {
        wake = audio_cache_wait_handle(f->config.cache); if (wake != NULL) { handles[count++] = wake; }
        wake = audio_cache_retry_handle(f->config.cache); if (wake != NULL) { handles[count++] = wake; }
    }
    return count;
}
uint32_t range_next_wait_ms(const RangeFetcher *f)
{
    if (f == NULL) { return UINT32_MAX; } if (f->needs_drive) { return 0; }
    uint64_t deadline = CORE_CLOCK_NEVER;
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) {
        const RangeRequest *r = &f->requests[i];
        if (r->id.value != 0 && r->result == RANGE_PENDING && r->deadline < deadline) { deadline = r->deadline; }
    }
    uint32_t wait = core_deadline_wait(core_clock_now(&f->config.clock), deadline), http = range_transfer_wait(f);
    return http < wait ? http : wait;
}
RangeResult range_fetcher_shutdown(RangeFetcher *f)
{
    if (f == NULL) { return RANGE_INVALID; } if (f->busy) { return RANGE_BAD_STATE; }
    f->shutdown = true;
    for (size_t i = 0; i < RANGE_SOURCES; ++i) { if (f->sources[i].live) { (void)range_cancel_generation(f, f->sources[i].id); } }
    RangeResult result = range_poll(f); if (result != RANGE_OK) { return result; }
    RangeStats stats = range_stats(f);
    if (f->active >= 0 || stats.leases != 0 || stats.states[RANGE_EMPTY] != RANGE_BLOCKS) { return RANGE_PENDING; }
    memset(f->requests, 0, sizeof(f->requests)); return RANGE_OK;
}
