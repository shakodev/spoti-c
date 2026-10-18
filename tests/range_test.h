#ifndef SPOTIC_RANGE_TEST_H
#define SPOTIC_RANGE_TEST_H
#include <winsock2.h>
#include "audio/range_internal.h"
#include "audio/cache_internal.h"
#include "support/http_server.h"
#include "support/test_check.h"
#include <stdatomic.h>
#include <wchar.h>
typedef struct RangePlan { TestHttpResponse replies[16]; char headers[16][512]; size_t count; } RangePlan;
typedef struct RangeTest {
    Arena core, http_arena, meta, pool, disk;
    Metrics metrics; MetricsArena bindings[5];
    Worker *worker; HttpClient *http; AudioCache *cache; RangeFetcher *fetcher;
    TestHttpServer server; bool server_live;
    wchar_t directory[MAX_PATH], file[MAX_PATH];
    MediaSource media;
    RangeSource source;
    _Atomic uint64_t now;
    uint64_t quota, write_limit; size_t entry_limit;
    size_t callbacks, outcomes[CACHE_BAD_STATE + 1]; CacheResult cache_result; CacheRecord record;
    DWORD owner_thread;
    size_t initial_pool, initial_meta;
    bool refresh_pending, refresh_changed; size_t refreshes;
} RangeTest;
extern RangeTest rt;
void rt_plan(RangePlan *, size_t index, uint64_t offset, uint64_t total, size_t bytes, unsigned char fill);
void rt_open(RangePlan *, bool cache, uint64_t quota, uint64_t write_limit, size_t limit);
void rt_reopen(void);
void rt_cache_stop(void);
void rt_cache_start(void);
void rt_close(void);
void rt_wait(void);
void rt_cache_drain(void);
void rt_cache_done(void *, uint64_t, CacheResult, const CacheRecord *);
CacheResult rt_cache_finish(void);
RangeId rt_request(uint64_t, size_t, RangePriority);
RangeResult rt_finish(RangeId);
void rt_check_lease(const RangeLease *, unsigned char);
void cache_disk_tests(void);
void range_error_tests(void);
void range_race_tests(void);
#endif
