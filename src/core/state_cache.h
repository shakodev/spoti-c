#ifndef SPOTIC_STATE_CACHE_H
#define SPOTIC_STATE_CACHE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

enum { STATE_CACHE_MAX_BYTES=256*1024, STATE_CACHE_ENTITIES=128,
    STATE_CACHE_LABEL=96, STATE_CACHE_HEADER=48, STATE_CACHE_SETTINGS=96,
    STATE_CACHE_ENTRY=116, STATE_CACHE_WIRE_MAX=48+96+128*116 };
typedef enum StateCacheResult { STATE_CACHE_OK, STATE_CACHE_MISSING, STATE_CACHE_INVALID,
    STATE_CACHE_VERSION, STATE_CACHE_CORRUPT, STATE_CACHE_FULL, STATE_CACHE_IO,
    STATE_CACHE_CANCELLED, STATE_CACHE_STALE } StateCacheResult;
typedef struct StateCacheHeader {
    uint32_t magic, version, header_bytes, length, checksum, reserved;
    uint64_t epoch;
    uint32_t settings_offset, settings_bytes, entities_offset, entities_count;
} StateCacheHeader; /* Documentation only: never serialize this struct. */
_Static_assert(sizeof(StateCacheHeader)==STATE_CACHE_HEADER,"cache header documentation matches field sizes");
typedef struct StateCacheEntity { uint32_t kind; uint8_t id[16]; char label[STATE_CACHE_LABEL]; } StateCacheEntity;
typedef struct StateCache {
    uint64_t epoch;
    int32_t x,y;
    uint32_t width,height,route,detail,volume,repeat,shuffle,scroll,route_kind;
    uint8_t route_id[16],track_id[16];
    uint32_t count;
    StateCacheEntity entities[STATE_CACHE_ENTITIES];
} StateCache;
/* Optional worker cancellation/fault boundary, called after flush BEFORE commit.
 * False preserves the old final file. Callback must not retain arguments. */
typedef bool (*StateCacheCommit)(void *context);
void state_cache_defaults(StateCache *, uint64_t epoch);
StateCacheResult state_cache_encode(const StateCache *, void *, size_t, size_t *written);
StateCacheResult state_cache_decode(const void *, size_t, uint64_t epoch, StateCache *);
StateCacheResult state_cache_load(const wchar_t *, uint64_t epoch, StateCache *);
/* Blocking disk operation: call on worker only. Unique temp, flush then rename;
 * concurrent writers commit whole snapshots with last successful replace wins. */
StateCacheResult state_cache_save(const wchar_t *, const StateCache *, StateCacheCommit, void *);
#endif
