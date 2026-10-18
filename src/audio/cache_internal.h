#ifndef SPOTIC_CACHE_INTERNAL_H
#define SPOTIC_CACHE_INTERNAL_H
#include "audio_cache.h"
#include "core/memory_budget.h"
#include <string.h>
enum { CACHE_HEADER = 64, CACHE_RECORD = 64 + AUDIO_CACHE_BLOCK };
enum CacheOperation { CO_OPEN, CO_READ, CO_WRITE, CO_CLOSE };
enum CacheJobState { CJ_FREE, CJ_ACTIVE, CJ_DONE, CJ_DELIVER };
typedef struct CacheEntry {
    uint8_t file_id[20]; uint32_t bytes;
    uint64_t offset, total, stamp;
    uint32_t checksum; bool valid;
} CacheEntry;
typedef struct CacheJob {
    struct AudioCache *owner;
    enum CacheOperation operation;
    enum CacheJobState state;
    CacheRecord record;
    void *data;
    CacheCallback callback;
    void *context;
    uint64_t ticket;
    CacheResult result;
} CacheJob;
struct AudioCache {
    AudioCacheConfig config;
    wchar_t path[MAX_PATH];
    HANDLE wake, file, io;
    MetricsHandle wake_metric, file_metric, io_metric;
    SRWLOCK lock;
    CacheResult status;
    AudioCacheStats stats;
    bool closing, close_queued, busy, worker_blocked;
    size_t extent;
    uint64_t stamp, written;
    CacheEntry entries[AUDIO_CACHE_ENTRIES];
    CacheJob jobs[AUDIO_CACHE_JOBS];
};
_Static_assert(sizeof(AudioCache) <= MEMORY_BUDGET_METADATA, "bounded disk cache index and jobs");
CacheResult cache_disk_open(AudioCache *, const CancelToken *);
CacheResult cache_disk_get(CacheJob *, const CancelToken *);
CacheResult cache_disk_put(CacheJob *, const CancelToken *);
CacheResult cache_disk_close(AudioCache *);
CacheResult cache_io(AudioCache *, bool write, uint64_t offset, void *, size_t, const CancelToken *);
CacheResult cache_disk_error(AudioCache *, DWORD);
uint32_t cache_checksum(const void *, size_t);
void cache_encode(uint8_t header[64], const CacheEntry *);
bool cache_decode(const uint8_t header[64], CacheEntry *);
uint32_t cache_u32(const uint8_t *);
uint64_t cache_u64(const uint8_t *);
void cache_set32(uint8_t *, uint32_t);
void cache_set64(uint8_t *, uint64_t);
#endif
