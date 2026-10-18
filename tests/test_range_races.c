#include "range_test.h"

typedef struct Gate { HANDLE entered, release; } Gate;
static void hold_worker(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; Gate *g = context; CHECK(SetEvent(g->entered));
    HANDLE handles[] = {g->release, worker_cancel_handle(cancel)};
    CHECK(WaitForMultipleObjects(2, handles, FALSE, 5000) == WAIT_OBJECT_0);
}
static void done(const Event *event, WorkerOutcome result, void *context)
{
    (void)event; (void)context; CHECK(result == WORKER_DONE);
}
static void cancelled_disk_generation(void)
{
    RangePlan plan = {0}; rt_plan(&plan, 0, 0, RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x93);
    rt_open(&plan, true, 0, 0, 0);
    Gate gate = {.entered = CreateEventW(NULL, TRUE, FALSE, NULL), .release = CreateEventW(NULL, TRUE, FALSE, NULL)};
    CHECK(gate.entered != NULL && gate.release != NULL);
    WorkerJob job = {.run = hold_worker, .complete = done, .context = &gate, .deadline = CORE_CLOCK_NEVER, .priority = WORKER_DISK};
    uint64_t ticket; CHECK(worker_submit(rt.worker, &job, &ticket) == CORE_OK);
    CHECK(WaitForSingleObject(gate.entered, 5000) == WAIT_OBJECT_0);
    RangeSource old = rt.source; RangeId cancelled = rt_request(0, 1, RANGE_CURRENT);
    CHECK(range_poll(rt.fetcher) == RANGE_OK && audio_cache_stats(rt.cache).jobs == 1 && range_stats(rt.fetcher).network_attempts == 0);
    CHECK(range_cancel_generation(rt.fetcher, old) == RANGE_OK);
    CHECK(range_source_open(rt.fetcher, &rt.media, &rt.source) == RANGE_OK && rt.source.slot != old.slot);
    RangeId fresh = rt_request(0, RANGE_BLOCK_BYTES, RANGE_CURRENT);
    CHECK(range_poll(rt.fetcher) == RANGE_OK && audio_cache_stats(rt.cache).jobs == 2);
    CHECK(SetEvent(gate.release) && rt_finish(fresh) == RANGE_OK && range_status(rt.fetcher, cancelled, false) == RANGE_CANCELLED);
    RangeSource recycled; CHECK(range_source_open(rt.fetcher, &rt.media, &recycled) == RANGE_OK && recycled.slot == old.slot && recycled.generation != old.generation);
    CHECK(range_acquire(rt.fetcher, old, 0, 1, &(RangeLease){0}) == RANGE_STALE);
    RangeLease lease = {0}; CHECK(range_acquire(rt.fetcher, rt.source, 0, RANGE_BLOCK_BYTES, &lease) == RANGE_OK); rt_check_lease(&lease, 0x93);
    CHECK(range_stats(rt.fetcher).network_attempts == 1 && range_release(rt.fetcher, &lease) == RANGE_OK);
    CHECK(CloseHandle(gate.entered) && CloseHandle(gate.release)); rt_close();
}
typedef struct Reader { RangeLease lease; HANDLE ready, check, checked, stop; } Reader;
static DWORD WINAPI read_while_evicting(void *context)
{
    Reader *r = context; CHECK(SetEvent(r->ready)); HANDLE handles[] = {r->stop, r->check};
    for (;;) {
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 5000);
        if (wait == WAIT_OBJECT_0) { return 0; }
        CHECK(wait == WAIT_OBJECT_0 + 1); rt_check_lease(&r->lease, 0x71); CHECK(SetEvent(r->checked));
    }
}
static void pinned_reader_and_eviction(void)
{
    RangePlan plan = {0}; const uint64_t total = 12 * (uint64_t)RANGE_BLOCK_BYTES;
    rt_plan(&plan, 0, 0, total, RANGE_BLOCK_BYTES, 0x71);
    for (size_t i = 1; i < 12; ++i) { rt_plan(&plan, i, i * (uint64_t)RANGE_BLOCK_BYTES, total, RANGE_BLOCK_BYTES, 0x55); }
    rt_open(&plan, true, 0, 0, 0); RangeId first = rt_request(0, RANGE_BLOCK_BYTES, RANGE_CURRENT); CHECK(rt_finish(first) == RANGE_OK);
    Reader reader = {.ready = CreateEventW(NULL, TRUE, FALSE, NULL), .check = CreateEventW(NULL, FALSE, FALSE, NULL),
        .checked = CreateEventW(NULL, FALSE, FALSE, NULL), .stop = CreateEventW(NULL, TRUE, FALSE, NULL)};
    CHECK(reader.ready != NULL && reader.check != NULL && reader.checked != NULL && reader.stop != NULL);
    CHECK(range_acquire(rt.fetcher, rt.source, 0, RANGE_BLOCK_BYTES, &reader.lease) == RANGE_OK && range_status(rt.fetcher, first, true) == RANGE_OK);
    HANDLE thread = CreateThread(NULL, 0, read_while_evicting, &reader, 0, NULL); CHECK(thread != NULL && WaitForSingleObject(reader.ready, 5000) == WAIT_OBJECT_0);
    for (size_t i = 1; i < 12; ++i) {
        RangeId next = rt_request(i * (uint64_t)RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, RANGE_CURRENT);
        CHECK(range_poll(rt.fetcher) == RANGE_OK && SetEvent(reader.check));
        CHECK(rt_finish(next) == RANGE_OK && WaitForSingleObject(reader.checked, 5000) == WAIT_OBJECT_0);
        RangeLease current = {0}; CHECK(range_acquire(rt.fetcher, rt.source, i * (uint64_t)RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, &current) == RANGE_OK);
        rt_check_lease(&current, 0x55); CHECK(range_release(rt.fetcher, &current) == RANGE_OK && range_status(rt.fetcher, next, true) == RANGE_OK);
    }
    CHECK(SetEvent(reader.check) && WaitForSingleObject(reader.checked, 5000) == WAIT_OBJECT_0);
    CHECK(SetEvent(reader.stop) && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CHECK(range_release(rt.fetcher, &reader.lease) == RANGE_OK);
    CHECK(CloseHandle(thread) && CloseHandle(reader.ready) && CloseHandle(reader.check) && CloseHandle(reader.checked) && CloseHandle(reader.stop)); rt_close();
}
void range_race_tests(void) { cancelled_disk_generation(); pinned_reader_and_eviction(); }
