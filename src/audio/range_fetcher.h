#ifndef SPOTIC_RANGE_FETCHER_H
#define SPOTIC_RANGE_FETCHER_H
#include "audio_cache.h"
#include "api/http_client.h"
#include "protocol/media_source.h"

enum { RANGE_BLOCKS = 8, RANGE_BLOCK_BYTES = 65536, RANGE_REQUESTS = 16, RANGE_LEASES = 16, RANGE_SOURCES = 4 };
typedef struct RangeFetcher RangeFetcher;
typedef struct RangeSource { uint64_t generation; uint32_t slot, reserved; } RangeSource;
typedef struct RangeId { uint64_t value; } RangeId;
typedef enum RangeResult { RANGE_OK, RANGE_PENDING, RANGE_FULL, RANGE_INVALID, RANGE_LIMIT,
    RANGE_STALE, RANGE_CLOSED, RANGE_CANCELLED, RANGE_TIMEOUT, RANGE_EOF, RANGE_HEADER,
    RANGE_TRUNCATED, RANGE_UNSUPPORTED, RANGE_HTTP_ERROR, RANGE_EXPIRED, RANGE_CHANGED,
    RANGE_EXHAUSTED, RANGE_BAD_STATE } RangeResult;
typedef enum RangeState { RANGE_EMPTY, RANGE_FETCHING, RANGE_READY, RANGE_PINNED } RangeState;
typedef enum RangePriority { RANGE_CURRENT, RANGE_PREFETCH } RangePriority;
typedef struct RangeSpan { const uint8_t *data; size_t bytes; uint64_t offset; } RangeSpan;
typedef struct RangeLease { uint64_t ticket; RangeSource source; size_t count, bytes; RangeSpan spans[RANGE_BLOCKS]; } RangeLease;
typedef struct RangeConfig {
    HttpClient *http;
    AudioCache *cache; /* Optional. Keep alive until fetcher shutdown completes. */
    CoreClock clock;
    uint32_t timeout_ms, max_attempts; /* Zero: 8000 ms, three attempts; cap eight. */
    /* Owner seam normally calls media_source_get after driving re-resolution.
     * Same file ID is mandatory; PENDING waits on owner's module event/deadline.
     * No refresh callback when all requested bytes are already cached. */
    MediaResult (*refresh)(void *, RangeSource, MediaSource *);
    void *context;
    bool fixture_loopback_http; /* Explicit local fixture opt-in only. */
} RangeConfig;
typedef struct RangeStats {
    size_t states[4], pending, leases, block_pool_bytes;
    uint64_t network_attempts, cache_hits, cache_errors, preemptions;
} RangeStats;
/* Network owner only. One fixed metadata allocation plus EXACTLY eight 64 KiB
 * blocks in a separate <=512 KiB arena; no heap or per-request allocation.
 * Only encrypted bytes are exposed/persisted. Not a UI or WASAPI callback API. */
RangeResult range_fetcher_create(Arena *metadata, Arena *blocks, const RangeConfig *, RangeFetcher **);
RangeResult range_source_open(RangeFetcher *, const MediaSource *, RangeSource *);
RangeResult range_source_size(RangeFetcher *, RangeSource, uint64_t *);
RangeResult range_request(RangeFetcher *, RangeSource, uint64_t offset, size_t length,
    RangePriority, RangeId *);
/* Completed requests reserve their blocks until consumed. Acquire BEFORE
 * consuming status, then pass immutable spans to decoder/read workers. */
RangeResult range_status(RangeFetcher *, RangeId, bool consume);
RangeResult range_acquire(RangeFetcher *, RangeSource, uint64_t offset, size_t length, RangeLease *);
RangeResult range_release(RangeFetcher *, RangeLease *); /* Exact one-use ticket. */
RangeResult range_cancel_generation(RangeFetcher *, RangeSource);
RangeResult range_poll(RangeFetcher *);
/* Owner waits on these borrowed handles plus the nearest deadline below and
 * its other modules (including the source resolver when refresh is pending).
 * Call again after polling/admission; the conditional worker handle can change. */
size_t range_wait_handles(const RangeFetcher *, HANDLE handles[3]);
uint32_t range_next_wait_ms(const RangeFetcher *);
RangeStats range_stats(const RangeFetcher *);
RangeResult range_fetcher_shutdown(RangeFetcher *); /* PENDING retains blocks/leases. */
#endif
