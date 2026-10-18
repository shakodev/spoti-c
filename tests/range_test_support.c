#include "range_test.h"

RangeTest rt;
static _Alignas(16) uint8_t core_memory[MEMORY_BUDGET_CORE], http_memory[MEMORY_BUDGET_HTTP];
static _Alignas(16) uint8_t meta_memory[65536], block_memory[MEMORY_BUDGET_ENCRYPTED_BLOCKS], disk_memory[MEMORY_BUDGET_METADATA];
static unsigned serial;
static uint64_t now(void *context) { (void)context; return atomic_load(&rt.now); }
static MediaResult refresh(void *context, RangeSource source, MediaSource *out)
{
    (void)context; (void)source; ++rt.refreshes;
    CHECK(range_request(rt.fetcher, rt.source, 0, 1, RANGE_CURRENT, &(RangeId){0}) == RANGE_BAD_STATE);
    if (rt.refresh_pending) { return MEDIA_PENDING; }
    *out = rt.media; out->urls[0].refresh_at = now(NULL) + 60000;
    if (rt.refresh_changed) { out->track.file_id[0] ^= 1; } return MEDIA_OK;
}
void rt_plan(RangePlan *plan, size_t index, uint64_t offset, uint64_t total, size_t bytes, unsigned char fill)
{
    CHECK(index < 16 && bytes > 0 && bytes <= RANGE_BLOCK_BYTES);
    CHECK(sprintf_s(plan->headers[index], sizeof(plan->headers[index]),
        "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes %llu-%llu/%llu\r\nContent-Length: %zu\r\nETag: \"encrypted-fixture\"\r\nConnection: close\r\n\r\n",
        (unsigned long long)offset, (unsigned long long)(offset + bytes - 1), (unsigned long long)total, bytes) > 0);
    plan->replies[index] = (TestHttpResponse){.headers = plan->headers[index], .bytes = bytes, .fragment = 4096, .fill = fill};
    if (plan->count <= index) { plan->count = index + 1; }
}
static void create_cache_and_fetcher(bool cache)
{
    if (cache) {
        AudioCacheConfig config = {.worker = rt.worker, .directory = rt.directory, .metrics = &rt.metrics,
            .quota = rt.quota, .fixture_write_limit = rt.write_limit, .entry_limit = rt.entry_limit};
        CHECK(audio_cache_create(&rt.disk, &config, &rt.cache) == CACHE_OK);
        uint64_t until = GetTickCount64() + 5000;
        while (audio_cache_status(rt.cache) == CACHE_PENDING) {
            CHECK(GetTickCount64() < until && WaitForSingleObject(audio_cache_wait_handle(rt.cache), 1000) != WAIT_FAILED);
            CHECK(audio_cache_poll(rt.cache) == CACHE_OK);
        }
    }
    RangeConfig config = {.http = rt.http, .cache = rt.cache, .clock = {now, NULL}, .refresh = refresh, .fixture_loopback_http = true};
    CHECK(range_fetcher_create(&rt.meta, &rt.pool, &config, &rt.fetcher) == RANGE_OK);
    CHECK(range_source_open(rt.fetcher, &rt.media, &rt.source) == RANGE_OK);
    rt.initial_pool = rt.pool.used; rt.initial_meta = rt.meta.used;
}
void rt_open(RangePlan *plan, bool cache, uint64_t quota, uint64_t write_limit, size_t limit)
{
    memset(&rt, 0, sizeof(rt)); atomic_init(&rt.now, 1000); rt.quota = quota; rt.write_limit = write_limit; rt.entry_limit = limit;
    rt.owner_thread = GetCurrentThreadId();
    CHECK(metrics_init(&rt.metrics));
    CHECK(arena_init(&rt.core, core_memory, sizeof(core_memory)) && arena_init(&rt.http_arena, http_memory, sizeof(http_memory)));
    CHECK(arena_init(&rt.meta, meta_memory, sizeof(meta_memory)) && arena_init(&rt.pool, block_memory, sizeof(block_memory)));
    CHECK(arena_init(&rt.disk, disk_memory, sizeof(disk_memory)));
    Arena *arenas[] = {&rt.core, &rt.http_arena, &rt.meta, &rt.pool, &rt.disk};
    for (size_t i = 0; i < 5; ++i) { CHECK(metrics_attach_arena(&rt.metrics, arenas[i], &rt.bindings[i])); }
    CHECK(worker_start(&rt.core, 16, (CoreClock){0}, &rt.worker) == CORE_OK);
    HttpConfig http = {.metrics = &rt.metrics, .allow_loopback_http = true};
    CHECK(http_client_create(&rt.http_arena, &http, &rt.http) == HTTP_OK);
    if (plan != NULL && plan->count != 0) {
        CHECK(test_http_server_start(&rt.server, &rt.metrics, plan->replies, plan->count)); rt.server_live = true;
    }
    wchar_t root[MAX_PATH]; CHECK(GetFullPathNameW(L".", MAX_PATH, root, NULL) > 0);
    CHECK(swprintf_s(rt.directory, MAX_PATH, L"%ls\\range-cache-test-%lu-%u", root, (unsigned long)GetCurrentProcessId(), ++serial) > 0);
    CHECK(swprintf_s(rt.file, MAX_PATH, L"%ls\\encrypted-v1.bin", rt.directory) > 0);
    memset(rt.media.track.file_id, 0x42, 20); rt.media.url_count = 1;
    CHECK(sprintf_s(rt.media.urls[0].url, MEDIA_URL_BYTES, "http://127.0.0.1:%u/encrypted", rt.server_live ? (unsigned)rt.server.port : 9u) > 0);
    rt.media.urls[0].refresh_at = 61000; create_cache_and_fetcher(cache);
}
void rt_wait(void)
{
    HANDLE handles[3]; DWORD count = (DWORD)range_wait_handles(rt.fetcher, handles); CHECK(count > 0);
    uint32_t wait = range_next_wait_ms(rt.fetcher); if (wait > 1000) { wait = 1000; }
    CHECK(WaitForMultipleObjects(count, handles, FALSE, wait) != WAIT_FAILED);
}
void rt_cache_drain(void)
{
    uint64_t until = GetTickCount64() + 5000;
    while (rt.cache != NULL && audio_cache_stats(rt.cache).jobs != 0) {
        CHECK(GetTickCount64() < until && range_poll(rt.fetcher) == RANGE_OK);
        if (audio_cache_stats(rt.cache).jobs != 0) { rt_wait(); }
    }
}
static void close_cache(void)
{
    uint64_t until = GetTickCount64() + 5000;
    CacheResult result;
    while ((result = audio_cache_shutdown(rt.cache)) == CACHE_PENDING) {
        CHECK(GetTickCount64() < until);
        HANDLE handle = audio_cache_wait_handle(rt.cache); CHECK(handle != NULL && WaitForSingleObject(handle, 1000) != WAIT_FAILED);
    }
    CHECK(result == CACHE_OK);
    CHECK(rt.cache->file == INVALID_HANDLE_VALUE && rt.cache->io == NULL && audio_cache_stats(rt.cache).jobs == 0);
}
void rt_cache_stop(void)
{
    rt_cache_drain(); CHECK(range_fetcher_shutdown(rt.fetcher) == RANGE_OK); close_cache();
}
void rt_cache_start(void)
{
    CHECK(arena_reset(&rt.meta) && arena_reset(&rt.pool) && arena_reset(&rt.disk));
    rt.cache = NULL; create_cache_and_fetcher(true);
}
void rt_reopen(void)
{
    rt_cache_stop(); rt_cache_start();
}
void rt_close(void)
{
    uint64_t until = GetTickCount64() + 5000;
    RangeResult result;
    while ((result = range_fetcher_shutdown(rt.fetcher)) == RANGE_PENDING) { CHECK(GetTickCount64() < until); rt_wait(); }
    CHECK(result == RANGE_OK);
    RangeStats stats = range_stats(rt.fetcher);
    CHECK(stats.states[RANGE_EMPTY] == RANGE_BLOCKS && stats.leases == 0);
    CHECK(rt.pool.used == rt.initial_pool && rt.meta.used == rt.initial_meta && rt.pool.used == MEMORY_BUDGET_ENCRYPTED_BLOCKS);
    if (rt.cache != NULL) { close_cache(); }
    CHECK(http_client_shutdown(rt.http, 5000) == HTTP_OK);
    CHECK(worker_stop(rt.worker) == CORE_OK && worker_join(rt.worker, 5000) == CORE_OK);
    if (rt.server_live) { test_http_server_stop(&rt.server); }
    if (rt.cache != NULL) {
        CHECK(DeleteFileW(rt.file) || GetLastError() == ERROR_FILE_NOT_FOUND);
        CHECK(RemoveDirectoryW(rt.directory) || GetLastError() == ERROR_PATH_NOT_FOUND);
    }
    arena_dispose(&rt.meta); arena_dispose(&rt.pool); arena_dispose(&rt.disk); arena_dispose(&rt.http_arena); arena_dispose(&rt.core);
    CHECK(metrics_balanced(&rt.metrics));
}
RangeId rt_request(uint64_t offset, size_t bytes, RangePriority priority)
{
    RangeId id; CHECK(range_request(rt.fetcher, rt.source, offset, bytes, priority, &id) == RANGE_OK); return id;
}
RangeResult rt_finish(RangeId id)
{
    uint64_t until = GetTickCount64() + 8000;
    for (;;) {
        CHECK(GetTickCount64() < until && range_poll(rt.fetcher) == RANGE_OK);
        RangeResult result = range_status(rt.fetcher, id, false); if (result != RANGE_PENDING) { return result; } rt_wait();
    }
}
void rt_cache_done(void *context, uint64_t ticket, CacheResult result, const CacheRecord *record)
{
    (void)context; CHECK(ticket != 0 && GetCurrentThreadId() == rt.owner_thread && (unsigned)result <= CACHE_BAD_STATE);
    ++rt.callbacks; ++rt.outcomes[result]; rt.cache_result = result; rt.record = *record;
    CHECK(audio_cache_cancel(rt.cache, ticket) == CACHE_BAD_STATE);
}
CacheResult rt_cache_finish(void)
{
    size_t before = rt.callbacks; uint64_t until = GetTickCount64() + 5000;
    while (rt.callbacks == before) {
        CHECK(GetTickCount64() < until && audio_cache_poll(rt.cache) == CACHE_OK);
        if (rt.callbacks == before) { CHECK(WaitForSingleObject(audio_cache_wait_handle(rt.cache), 1000) != WAIT_FAILED); }
    }
    return rt.cache_result;
}
void rt_check_lease(const RangeLease *lease, unsigned char fill)
{
    for (size_t i = 0; i < lease->count; ++i) { for (size_t j = 0; j < lease->spans[i].bytes; ++j) { CHECK(lease->spans[i].data[j] == fill); } }
}
