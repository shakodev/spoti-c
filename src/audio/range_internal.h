#ifndef SPOTIC_RANGE_INTERNAL_H
#define SPOTIC_RANGE_INTERNAL_H
#include "range_fetcher.h"
#include <string.h>
typedef struct RangeSourceRecord {
    RangeSource id;
    uint8_t file_id[20];
    bool live, cancelled, size_known;
    uint64_t total;
    RangeResult failure;
    MediaUrl urls[MEDIA_URLS]; size_t url_count;
    wchar_t etag[128];
} RangeSourceRecord;
typedef struct RangeBlock {
    struct RangeFetcher *owner;
    RangeSource source;
    RangeState state;
    uint64_t offset, total, disk_ticket, stamp;
    size_t bytes, references;
    uint32_t attempts;
    size_t url;
    bool disk_read, disk_write, cache_tried, cache_stored, preempted;
    RangePriority priority;
    RangeResult failure;
} RangeBlock;
typedef struct RangeRequest {
    RangeId id; RangeSource source;
    uint64_t offset, deadline; size_t bytes;
    RangePriority priority; RangeResult result;
} RangeRequest;
typedef struct RangeTicket { uint64_t value; RangeSource source; uint8_t blocks; } RangeTicket;
struct RangeFetcher {
    RangeConfig config;
    uint8_t *pool;
    RangeSourceRecord sources[RANGE_SOURCES];
    RangeBlock blocks[RANGE_BLOCKS];
    RangeRequest requests[RANGE_REQUESTS];
    RangeTicket leases[RANGE_LEASES];
    uint64_t generation, sequence, ticket, stamp;
    RangeStats stats;
    int active;
    HttpRequestId http_id;
    bool shutdown, busy, needs_drive;
};
_Static_assert(RANGE_BLOCKS * RANGE_BLOCK_BYTES == MEMORY_BUDGET_ENCRYPTED_BLOCKS, "512 KiB ciphertext pool");
_Static_assert(sizeof(RangeFetcher) < 64 * 1024, "bounded range metadata");
bool range_same(RangeSource, RangeSource);
RangeSourceRecord *range_source(RangeFetcher *, RangeSource);
RangeBlock *range_block(RangeFetcher *, RangeSource, uint64_t);
bool range_reserved(const RangeFetcher *, const RangeBlock *, bool current_only);
void range_clear_block(RangeFetcher *, RangeBlock *);
void range_reap_sources(RangeFetcher *);
RangeResult range_set_size(RangeSourceRecord *, uint64_t);
void range_update_requests(RangeFetcher *);
void range_cache_complete(void *, uint64_t, CacheResult, const CacheRecord *);
RangeResult range_http_poll(RangeFetcher *);
RangeResult range_schedule(RangeFetcher *);
RangeResult range_parse_response(const HttpCompletion *, uint64_t first, uint64_t last,
    uint64_t *total, size_t *bytes, wchar_t etag[128]);
bool range_urls_valid(const RangeFetcher *, const MediaSource *);
#endif
