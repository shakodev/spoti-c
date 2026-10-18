#include "range_test.h"

static uint8_t input[AUDIO_CACHE_BLOCK], output[AUDIO_CACHE_BLOCK];
static CacheRecord record(uint64_t offset, uint64_t total)
{
    CacheRecord r = {.offset = offset, .total = total, .bytes = (size_t)(total - offset < AUDIO_CACHE_BLOCK ? total - offset : AUDIO_CACHE_BLOCK)};
    memset(r.file_id, 0x42, 20); return r;
}
static CacheResult put(CacheRecord r, unsigned char fill)
{
    memset(input, fill, sizeof(input)); uint64_t ticket;
    CHECK(audio_cache_put(rt.cache, &r, input, rt_cache_done, NULL, &ticket) == CACHE_OK); return rt_cache_finish();
}
static CacheResult get(CacheRecord r)
{
    memset(output, 0, sizeof(output)); uint64_t ticket;
    CHECK(audio_cache_get(rt.cache, &r, output, rt_cache_done, NULL, &ticket) == CACHE_OK); return rt_cache_finish();
}
static void check_output(unsigned char fill, size_t bytes)
{
    for (size_t i = 0; i < bytes; ++i) { CHECK(output[i] == fill); }
}
static void quota_and_holes(void)
{
    const uint64_t total = 3 * (uint64_t)AUDIO_CACHE_BLOCK + 7;
    rt_open(NULL, true, CACHE_HEADER + 2 * (uint64_t)CACHE_RECORD, 0, 2); CHECK(audio_cache_status(rt.cache) == CACHE_OK);
    CHECK(put(record(0, total), 0x99) == CACHE_OK && put(record(AUDIO_CACHE_BLOCK, total), 0x55) == CACHE_OK);
    CacheRecord lookup = record(0, total); lookup.total = 0;
    CHECK(get(lookup) == CACHE_OK && rt.record.total == total && rt.record.bytes == AUDIO_CACHE_BLOCK); check_output(0x99, sizeof(output));
    CHECK(put(record(2 * (uint64_t)AUDIO_CACHE_BLOCK, total), 0x77) == CACHE_OK);
    CHECK(get(record(AUDIO_CACHE_BLOCK, total)) == CACHE_MISS); /* LRU block one evicted; hole is explicit. */
    CHECK(get(record(0, total + 1)) == CACHE_CORRUPT && get(record(0, total)) == CACHE_OK);
    CacheRecord another_file = record(0, total); another_file.file_id[0] ^= 1;
    CHECK(get(another_file) == CACHE_MISS);
    uint64_t untouched = 13; CacheRecord incomplete = record(0, total); incomplete.bytes = 16;
    CHECK(audio_cache_put(rt.cache, &incomplete, input, rt_cache_done, NULL, &untouched) == CACHE_INVALID && untouched == 13);
    CHECK(put(record(0, total), 0x11) == CACHE_CORRUPT); /* Overlapping replacement may not change representation. */
    CHECK(audio_cache_stats(rt.cache).entries == 2 && audio_cache_stats(rt.cache).evictions == 1);
    CHECK(audio_cache_stats(rt.cache).file_bytes <= audio_cache_stats(rt.cache).quota);
    rt_reopen(); CHECK(get(record(0, total)) == CACHE_OK); check_output(0x99, sizeof(output));
    CHECK(get(record(AUDIO_CACHE_BLOCK, total)) == CACHE_MISS);
    CHECK(put(record(3 * (uint64_t)AUDIO_CACHE_BLOCK, total), 0x2a) == CACHE_OK);
    CHECK(get(record(3 * (uint64_t)AUDIO_CACHE_BLOCK, total)) == CACHE_OK && rt.record.bytes == 7); check_output(0x2a, 7);
    for (size_t i = 7; i < sizeof(output); ++i) { CHECK(output[i] == 0); }
    rt_close();
}
static void modify_file(uint64_t offset, const void *data, DWORD bytes, bool truncate)
{
    HANDLE file = CreateFileW(rt.file, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE); LARGE_INTEGER at; at.QuadPart = (LONGLONG)offset;
    CHECK(SetFilePointerEx(file, at, NULL, FILE_BEGIN));
    if (truncate) { CHECK(SetEndOfFile(file)); }
    else { DWORD written = 0; CHECK(WriteFile(file, data, bytes, &written, NULL) && written == bytes); }
    CHECK(CloseHandle(file));
}
static void corruption(void)
{
    CacheRecord r = record(0, AUDIO_CACHE_BLOCK);
    rt_open(NULL, true, 0, 0, 0); CHECK(put(r, 0x99) == CACHE_OK); rt_cache_stop();
    uint8_t byte = 0; modify_file(128, &byte, 1, false); rt_cache_start();
    CHECK(audio_cache_status(rt.cache) == CACHE_OK && get(r) == CACHE_CORRUPT && get(r) == CACHE_MISS);
    CHECK(put(r, 0x99) == CACHE_OK); rt_cache_stop();
    modify_file(64, &byte, 1, false); rt_cache_start();
    CHECK(get(r) == CACHE_MISS); rt_close(); /* Incomplete/header-damaged record is a hole. */
    rt_open(NULL, true, 0, 0, 0); rt_cache_stop();
    byte = 2; modify_file(8, &byte, 1, false); rt_cache_start();
    CHECK(audio_cache_status(rt.cache) == CACHE_VERSION); rt_close();
    rt_open(NULL, true, 0, 0, 0); CHECK(put(r, 1) == CACHE_OK); rt_cache_stop();
    modify_file(130, NULL, 0, true); rt_cache_start();
    CHECK(audio_cache_status(rt.cache) == CACHE_CORRUPT); rt_close();
    rt_open(NULL, true, 0, 128, 0);
    CHECK(put(r, 0x99) == CACHE_IO_ERROR && audio_cache_stats(rt.cache).os_error == ERROR_DISK_FULL);
    CHECK(get(r) == CACHE_MISS && audio_cache_stats(rt.cache).entries == 0); rt_close();
    rt_open(NULL, true, CACHE_HEADER + CACHE_RECORD, 0, 0); rt_cache_stop();
    modify_file(CACHE_HEADER + 2 * (uint64_t)CACHE_RECORD, NULL, 0, true); rt_cache_start();
    CHECK(audio_cache_status(rt.cache) == CACHE_QUOTA); rt_close();
}
typedef struct Barrier { HANDLE entered, release; _Atomic size_t complete; } Barrier;
static void block_worker(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; Barrier *b = context; CHECK(SetEvent(b->entered));
    HANDLE handles[] = {b->release, worker_cancel_handle(cancel)};
    CHECK(WaitForMultipleObjects(2, handles, FALSE, 5000) == WAIT_OBJECT_0);
}
static void block_done(const Event *event, WorkerOutcome result, void *context)
{
    (void)event; CHECK(result == WORKER_DONE); atomic_fetch_add(&((Barrier *)context)->complete, 1);
}
static void no_op(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; (void)cancel; (void)context;
}
static void cancellation_and_full(void)
{
    rt_open(NULL, true, 0, 0, 0);
    Barrier b = {.entered = CreateEventW(NULL, TRUE, FALSE, NULL), .release = CreateEventW(NULL, TRUE, FALSE, NULL)};
    atomic_init(&b.complete, 0); CHECK(b.entered != NULL && b.release != NULL);
    WorkerJob blocker = {.run = block_worker, .complete = block_done, .context = &b, .deadline = CORE_CLOCK_NEVER, .priority = WORKER_DISK};
    uint64_t worker_ticket, tickets[AUDIO_CACHE_JOBS], unchanged = 42;
    CHECK(worker_submit(rt.worker, &blocker, &worker_ticket) == CORE_OK && WaitForSingleObject(b.entered, 5000) == WAIT_OBJECT_0);
    CacheRecord r = record(0, AUDIO_CACHE_BLOCK);
    for (size_t i = 0; i < AUDIO_CACHE_JOBS; ++i) { CHECK(audio_cache_put(rt.cache, &r, input, rt_cache_done, NULL, &tickets[i]) == CACHE_OK); }
    CHECK(audio_cache_put(rt.cache, &r, input, rt_cache_done, NULL, &unchanged) == CACHE_FULL && unchanged == 42);
    CHECK(audio_cache_cancel(rt.cache, tickets[0]) == CACHE_OK && SetEvent(b.release));
    uint64_t until = GetTickCount64() + 5000;
    while (rt.callbacks < AUDIO_CACHE_JOBS) {
        CHECK(GetTickCount64() < until && audio_cache_poll(rt.cache) == CACHE_OK);
        if (rt.callbacks < AUDIO_CACHE_JOBS) { CHECK(WaitForSingleObject(audio_cache_wait_handle(rt.cache), 1000) != WAIT_FAILED); }
    }
    CHECK(rt.callbacks == AUDIO_CACHE_JOBS && atomic_load(&b.complete) == 1);
    CHECK(rt.outcomes[CACHE_CANCELLED] == 1 && rt.outcomes[CACHE_OK] == AUDIO_CACHE_JOBS - 1);
    CHECK(WaitForSingleObject(worker_idle_handle(rt.worker), 5000) == WAIT_OBJECT_0);
    CHECK(audio_cache_cancel(rt.cache, tickets[0]) == CACHE_STALE);
    CHECK(CloseHandle(b.entered) && CloseHandle(b.release)); rt_close();
}
static void worker_backpressure(void)
{
    rt_open(NULL, true, 0, 0, 0);
    Barrier b = {.entered = CreateEventW(NULL, TRUE, FALSE, NULL), .release = CreateEventW(NULL, TRUE, FALSE, NULL)};
    atomic_init(&b.complete, 0); CHECK(b.entered != NULL && b.release != NULL);
    WorkerJob job = {.run = block_worker, .complete = block_done, .context = &b, .deadline = CORE_CLOCK_NEVER, .priority = WORKER_DISK};
    uint64_t ticket; CHECK(worker_submit(rt.worker, &job, &ticket) == CORE_OK);
    CHECK(WaitForSingleObject(b.entered, 5000) == WAIT_OBJECT_0); job.run = no_op;
    for (size_t i = 1; i < rt.worker->capacity; ++i) { CHECK(worker_submit(rt.worker, &job, &ticket) == CORE_OK); }
    CacheRecord r = record(0, AUDIO_CACHE_BLOCK); ticket = 99;
    CHECK(audio_cache_get(rt.cache, &r, output, rt_cache_done, NULL, &ticket) == CACHE_FULL && ticket == 99);
    CHECK(audio_cache_stats(rt.cache).jobs == 0 && audio_cache_retry_handle(rt.cache) == worker_idle_handle(rt.worker));
    HANDLE handles[3]; CHECK(range_wait_handles(rt.fetcher, handles) == 3);
    CHECK(SetEvent(b.release) && WaitForMultipleObjects(3, handles, FALSE, 5000) == WAIT_OBJECT_0 + 2);
    CHECK(atomic_load(&b.complete) == rt.worker->capacity);
    CHECK(get(r) == CACHE_MISS && audio_cache_retry_handle(rt.cache) == NULL && range_wait_handles(rt.fetcher, handles) == 2);
    CHECK(CloseHandle(b.entered) && CloseHandle(b.release)); rt_close();
}
void cache_disk_tests(void) { quota_and_holes(); corruption(); cancellation_and_full(); worker_backpressure(); }
