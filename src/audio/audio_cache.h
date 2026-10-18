#ifndef SPOTIC_AUDIO_CACHE_H
#define SPOTIC_AUDIO_CACHE_H
#include "core/worker.h"

enum { AUDIO_CACHE_BLOCK = 65536, AUDIO_CACHE_ENTRIES = 4096, AUDIO_CACHE_JOBS = 8 };
#define AUDIO_CACHE_QUOTA (UINT64_C(256) * 1024 * 1024)
typedef struct AudioCache AudioCache;
typedef enum CacheResult { CACHE_OK, CACHE_PENDING, CACHE_MISS, CACHE_FULL,
    CACHE_INVALID, CACHE_CLOSED, CACHE_CANCELLED, CACHE_TIMEOUT, CACHE_CORRUPT,
    CACHE_VERSION, CACHE_IO_ERROR, CACHE_QUOTA, CACHE_STALE, CACHE_EXHAUSTED,
    CACHE_BAD_STATE } CacheResult;
typedef struct CacheRecord { uint8_t file_id[20]; uint64_t offset, total; size_t bytes; } CacheRecord;
typedef void (*CacheCallback)(void *, uint64_t ticket, CacheResult, const CacheRecord *);
typedef struct AudioCacheConfig {
    Worker *worker; /* Borrowed blocking worker, alive through cache shutdown. */
    const wchar_t *directory; /* Copied absolute existing-parent directory, <=240 chars. */
    Metrics *metrics;
    uint64_t quota; /* Zero: 256 MiB; header + fixed record slots count against it. */
    size_t entry_limit; /* Zero: 4096; fixed bounded index, never grows. */
    uint32_t io_timeout_ms; /* Zero: 3000; cancel I/O and retain buffers until retired. */
    uint64_t fixture_write_limit; /* Explicit deterministic disk-full fault; zero disables. */
} AudioCacheConfig;
typedef struct AudioCacheStats {
    size_t entries, capacity, jobs, index_bytes;
    uint64_t file_bytes, quota, hits, misses, evictions, errors;
    DWORD os_error;
} AudioCacheStats;
/* Network-owner admission/poll; all file and index operations run on worker.
 * One arena allocation <=256 KiB, no heap. Create queues background open.
 * Exactly one callback per admitted get/put, always on the polling owner.
 * Output/input 64 KiB block storage is borrowed through completion, even cancel.
 * Only ciphertext belongs here: no audio key/plaintext API or serialization. */
CacheResult audio_cache_create(Arena *, const AudioCacheConfig *, AudioCache **);
CacheResult audio_cache_status(const AudioCache *);
CacheResult audio_cache_get(AudioCache *, const CacheRecord *, void *block,
    CacheCallback, void *, uint64_t *ticket);
CacheResult audio_cache_put(AudioCache *, const CacheRecord *, const void *ciphertext,
    CacheCallback, void *, uint64_t *ticket);
CacheResult audio_cache_cancel(AudioCache *, uint64_t ticket);
CacheResult audio_cache_poll(AudioCache *);
HANDLE audio_cache_wait_handle(const AudioCache *); /* Borrowed completion wake. */
/* Include this conditional borrowed handle after CACHE_FULL from a shared
 * worker queue. It wakes for retry when that worker has retired its jobs. */
HANDLE audio_cache_retry_handle(const AudioCache *);
AudioCacheStats audio_cache_stats(AudioCache *);
CacheResult audio_cache_shutdown(AudioCache *); /* PENDING until disk/callbacks retire. */
#endif
