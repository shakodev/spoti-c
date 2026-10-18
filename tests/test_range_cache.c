#include "range_test.h"

static void pattern(const RangeLease *lease)
{
    for (size_t i = 0; i < lease->count; ++i) {
        const RangeSpan *span = &lease->spans[i];
        for (size_t j = 0; j < span->bytes; ++j) { CHECK(span->data[j] == (unsigned char)(span->offset / RANGE_BLOCK_BYTES + 1)); }
    }
}
static void ranges_and_pins(void)
{
    RangePlan plan = {0}; const uint64_t total = 9 * (uint64_t)RANGE_BLOCK_BYTES + 17;
    for (size_t i = 0; i < 10; ++i) { rt_plan(&plan, i, i * (uint64_t)RANGE_BLOCK_BYTES, total, i == 9 ? 17 : RANGE_BLOCK_BYTES, (unsigned char)(i + 1)); }
    rt_open(&plan, true, 0, 0, 0);
    CHECK(audio_cache_status(rt.cache) == CACHE_OK);
    CHECK(audio_cache_stats(rt.cache).index_bytes <= MEMORY_BUDGET_METADATA);
    CHECK(audio_cache_stats(rt.cache).quota == AUDIO_CACHE_QUOTA);
    uint64_t size = 0; CHECK(range_source_size(rt.fetcher, rt.source, &size) == RANGE_PENDING);
    RangeLease unavailable = {0}; CHECK(range_acquire(rt.fetcher, rt.source, 0, 1, &unavailable) == RANGE_PENDING);
    RangeId cross = rt_request(RANGE_BLOCK_BYTES - 10, 20, RANGE_CURRENT);
    CHECK(rt_finish(cross) == RANGE_OK && range_source_size(rt.fetcher, rt.source, &size) == RANGE_OK && size == total);
    RangeLease crossing = {0}; CHECK(range_acquire(rt.fetcher, rt.source, RANGE_BLOCK_BYTES - 10, 20, &crossing) == RANGE_OK);
    CHECK(crossing.count == 2 && crossing.bytes == 20 && crossing.spans[0].bytes == 10); pattern(&crossing);
    CHECK(range_status(rt.fetcher, cross, true) == RANGE_OK && range_release(rt.fetcher, &crossing) == RANGE_OK);
    RangeId all = rt_request(0, 8 * RANGE_BLOCK_BYTES, RANGE_CURRENT); CHECK(rt_finish(all) == RANGE_OK);
    CHECK(range_stats(rt.fetcher).block_pool_bytes == 512 * 1024);
    uint32_t random = 41;
    for (size_t i = 0; i < 100; ++i) {
        random = random * 1664525u + 1013904223u; uint64_t offset = random % (7 * RANGE_BLOCK_BYTES);
        size_t bytes = (random >> 16) + 1; RangeLease lease = {0};
        CHECK(range_acquire(rt.fetcher, rt.source, offset, bytes, &lease) == RANGE_OK); pattern(&lease);
        RangeLease copy = lease;
        CHECK(range_release(rt.fetcher, &lease) == RANGE_OK && range_release(rt.fetcher, &copy) == RANGE_STALE);
    }
    RangeLease pins[RANGE_LEASES] = {0};
    CHECK(range_acquire(rt.fetcher, rt.source, 0, 8 * RANGE_BLOCK_BYTES, &pins[0]) == RANGE_OK);
    for (size_t i = 1; i < RANGE_LEASES; ++i) { CHECK(range_acquire(rt.fetcher, rt.source, 0, 4, &pins[i]) == RANGE_OK); }
    CHECK(range_acquire(rt.fetcher, rt.source, 0, 1, &unavailable) == RANGE_FULL);
    CHECK(range_status(rt.fetcher, all, true) == RANGE_OK); rt_cache_drain();
    uint64_t attempts = range_stats(rt.fetcher).network_attempts;
    RangeId blocked = rt_request(8 * (uint64_t)RANGE_BLOCK_BYTES, 1, RANGE_CURRENT);
    CHECK(range_poll(rt.fetcher) == RANGE_OK && range_status(rt.fetcher, blocked, false) == RANGE_PENDING);
    CHECK(range_stats(rt.fetcher).network_attempts == attempts && range_next_wait_ms(rt.fetcher) != 0);
    pattern(&pins[0]);
    CHECK(range_release(rt.fetcher, &pins[0]) == RANGE_OK);
    CHECK(rt_finish(blocked) == RANGE_OK); /* Evict unpinned block, retain block zero. */
    for (size_t i = 1; i < RANGE_LEASES; ++i) { rt_check_lease(&pins[i], 1); CHECK(range_release(rt.fetcher, &pins[i]) == RANGE_OK); }
    RangeLease ninth = {0}; CHECK(range_acquire(rt.fetcher, rt.source, 8 * (uint64_t)RANGE_BLOCK_BYTES, 1, &ninth) == RANGE_OK);
    rt_check_lease(&ninth, 9); CHECK(range_status(rt.fetcher, blocked, true) == RANGE_OK && range_release(rt.fetcher, &ninth) == RANGE_OK);
    RangeId tail = rt_request(9 * (uint64_t)RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, RANGE_CURRENT);
    CHECK(rt_finish(tail) == RANGE_OK);
    RangeLease short_read = {0}; CHECK(range_acquire(rt.fetcher, rt.source, total - 17, RANGE_BLOCK_BYTES, &short_read) == RANGE_OK);
    CHECK(short_read.bytes == 17); rt_check_lease(&short_read, 10);
    CHECK(range_release(rt.fetcher, &short_read) == RANGE_OK && range_status(rt.fetcher, tail, true) == RANGE_OK);
    RangeId id; CHECK(range_request(rt.fetcher, rt.source, total, 1, RANGE_CURRENT, &id) == RANGE_EOF);
    rt_cache_drain(); CHECK(audio_cache_stats(rt.cache).entries == 10);
    CHECK(strstr(rt.server.requests[0], "Range: bytes=0-65535\r\n") != NULL);
    CHECK(strstr(rt.server.requests[0], "Accept-Encoding: identity\r\n") != NULL);
    CHECK(strstr(rt.server.requests[0], "Authorization:") == NULL && strstr(rt.server.requests[0], "Client-Token:") == NULL);
    rt_reopen(); /* Cached ciphertext works without an unexpired CDN URL. */
    rt.fetcher->sources[rt.source.slot].url_count = 0; rt.refresh_pending = true;
    all = rt_request(0, 8 * RANGE_BLOCK_BYTES, RANGE_CURRENT); CHECK(rt_finish(all) == RANGE_OK);
    CHECK(range_stats(rt.fetcher).cache_hits == 8 && range_stats(rt.fetcher).network_attempts == 0 && rt.refreshes == 0);
    RangeLease cached = {0}; CHECK(range_acquire(rt.fetcher, rt.source, 0, 8 * RANGE_BLOCK_BYTES, &cached) == RANGE_OK); pattern(&cached);
    CHECK(range_release(rt.fetcher, &cached) == RANGE_OK && range_status(rt.fetcher, all, true) == RANGE_OK); rt_close();
}
static void priorities(void)
{
    RangePlan plan = {0}; rt_plan(&plan, 0, RANGE_BLOCK_BYTES, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 2);
    rt_plan(&plan, 1, 0, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 1); rt_open(&plan, false, 0, 0, 0);
    RangeId prefetch = rt_request(0, 1, RANGE_PREFETCH), current = rt_request(RANGE_BLOCK_BYTES, 1, RANGE_CURRENT);
    CHECK(rt_finish(current) == RANGE_OK); CHECK(rt_finish(prefetch) == RANGE_OK);
    CHECK(strstr(rt.server.requests[0], "Range: bytes=65536-131071\r\n") != NULL); rt_close();
    memset(&plan, 0, sizeof(plan));
    rt_plan(&plan, 0, 0, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 1); plan.replies[0].hold_body = true;
    rt_plan(&plan, 1, RANGE_BLOCK_BYTES, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 2);
    rt_plan(&plan, 2, 0, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 1); rt_open(&plan, false, 0, 0, 0);
    prefetch = rt_request(0, 1, RANGE_PREFETCH); CHECK(range_poll(rt.fetcher) == RANGE_OK);
    CHECK(WaitForSingleObject(rt.server.headers_sent, 5000) == WAIT_OBJECT_0);
    current = rt_request(RANGE_BLOCK_BYTES, 1, RANGE_CURRENT); CHECK(SetEvent(rt.server.release_body));
    CHECK(rt_finish(current) == RANGE_OK && rt_finish(prefetch) == RANGE_OK && range_stats(rt.fetcher).preemptions == 1);
    CHECK(strstr(rt.server.requests[1], "Range: bytes=65536-131071\r\n") != NULL); rt_close();
}
static void random_seeks(void)
{
    const unsigned order[] = {14, 3, 9, 0, 11, 4, 15, 2, 7, 12, 1, 8}; RangePlan plan = {0};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        rt_plan(&plan, i, order[i] * (uint64_t)RANGE_BLOCK_BYTES, 16 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, (unsigned char)(order[i] + 1));
    }
    rt_open(&plan, false, 0, 0, 0);
    for (size_t i = 0; i < plan.count; ++i) {
        uint64_t offset = order[i] * (uint64_t)RANGE_BLOCK_BYTES + (i * 4099) % 65000;
        RangeId id = rt_request(offset, 511, RANGE_CURRENT); CHECK(rt_finish(id) == RANGE_OK);
        RangeLease lease = {0}; CHECK(range_acquire(rt.fetcher, rt.source, offset, 511, &lease) == RANGE_OK); pattern(&lease);
        CHECK(range_release(rt.fetcher, &lease) == RANGE_OK && range_status(rt.fetcher, id, true) == RANGE_OK);
    }
    CHECK(range_stats(rt.fetcher).network_attempts == plan.count); rt_close();
}
static void admission(void)
{
    rt_open(NULL, false, 0, 0, 0); RangeId ids[RANGE_REQUESTS], sentinel = {91};
    static _Alignas(16) uint8_t meta_memory[65536]; uint8_t tiny_pool[16]; Arena meta, pool;
    CHECK(arena_init(&meta, meta_memory, sizeof(meta_memory)) && arena_init(&pool, tiny_pool, sizeof(tiny_pool)));
    RangeConfig config = {.http = rt.http}; RangeFetcher *none = rt.fetcher;
    CHECK(range_fetcher_create(&meta, &pool, &config, &none) == RANGE_FULL && none == NULL && meta.used == 0 && pool.used == 0);
    arena_dispose(&meta); arena_dispose(&pool);
    CHECK(range_request(rt.fetcher, rt.source, UINT64_MAX - 1, 2, RANGE_CURRENT, &sentinel) == RANGE_INVALID);
    CHECK(range_request(rt.fetcher, rt.source, 1, 8 * RANGE_BLOCK_BYTES, RANGE_CURRENT, &sentinel) == RANGE_LIMIT);
    CHECK(range_request(rt.fetcher, rt.source, 0, 0, RANGE_CURRENT, &sentinel) == RANGE_INVALID);
    CHECK(sentinel.value == 91);
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) { ids[i] = rt_request(0, 1, RANGE_CURRENT); }
    CHECK(range_request(rt.fetcher, rt.source, 0, 1, RANGE_CURRENT, &sentinel) == RANGE_FULL);
    atomic_store(&rt.now, 9000); CHECK(range_poll(rt.fetcher) == RANGE_OK);
    for (size_t i = 0; i < RANGE_REQUESTS; ++i) { CHECK(range_status(rt.fetcher, ids[i], true) == RANGE_TIMEOUT); }
    CHECK(range_stats(rt.fetcher).network_attempts == 0);
    rt.fetcher->sequence = UINT64_MAX; CHECK(range_request(rt.fetcher, rt.source, 0, 1, RANGE_CURRENT, &sentinel) == RANGE_EXHAUSTED);
    RangeSource source; rt.fetcher->generation = UINT64_MAX;
    CHECK(range_source_open(rt.fetcher, &rt.media, &source) == RANGE_EXHAUSTED); rt_close();
}
int main(void)
{
    ranges_and_pins(); priorities(); random_seeks(); admission(); cache_disk_tests(); range_error_tests(); range_race_tests();
    printf("audio range/cache tests passed: pool=%zu, range state=%zu, cache state=%zu, index=%zu bytes\n",
        (size_t)MEMORY_BUDGET_ENCRYPTED_BLOCKS, sizeof(RangeFetcher), sizeof(AudioCache), sizeof(((AudioCache *)0)->entries)); return 0;
}
