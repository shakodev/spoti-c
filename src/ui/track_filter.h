#ifndef SPOTIC_TRACK_FILTER_H
#define SPOTIC_TRACK_FILTER_H
#include "core/worker.h"

#define TRACK_INDEX_CAP 10000u
#define TRACK_QUERY_BYTES 96u
#define TRACK_BATCH 64u
#define TRACK_FILTER_EVENT 0x3100u
enum { TRACK_ID=1, TRACK_METADATA=2, TRACK_PLAYABLE=4 };
typedef enum TrackResult { TRACK_OK, TRACK_PENDING, TRACK_FULL, TRACK_INVALID,
    TRACK_STALE, TRACK_CANCELLED, TRACK_IO } TrackResult;
typedef enum TrackCoverage { TRACK_FULL_INDEX, TRACK_LOADED_ONLY, TRACK_WINDOW } TrackCoverage;
typedef enum TrackSort { TRACK_SOURCE_ORDER, TRACK_TITLE_ORDER, TRACK_DURATION_ORDER } TrackSort;
typedef struct TrackRecord {
    uint8_t id[16];
    uint32_t duration_ms, flags;
    char title[96], artist[64], album[80];
} TrackRecord;
typedef struct TrackIdentity { uint8_t id[16]; uint32_t duration_ms; uint8_t flags, title_length; } TrackIdentity;
/* Immutable normalized index, NOT a Spotify response. read runs only on one
 * blocking worker; no UI/layout I/O. Source/context outlive cancel acknowledgement.
 * Missing metadata is explicit. Stable occurrence identity is revision+ordinal. */
typedef struct TrackSource {
    TrackResult (*read)(void *, uint32_t first, uint32_t count, TrackRecord *);
    void *context;
    uint32_t total;
    bool complete;
    char uri[192], revision[64];
} TrackSource;
typedef struct TrackJob {
    TrackSource source;
    uint64_t generation;
    uint32_t base, count, missing, rows[TRACK_BATCH];
    char query[TRACK_QUERY_BYTES];
    TrackSort sort;
    TrackCoverage coverage;
    TrackResult result;
    bool metadata;
} TrackJob;
typedef struct TrackFilter {
    TrackIdentity index[TRACK_INDEX_CAP];
    uint32_t map[TRACK_INDEX_CAP], sort_keys[TRACK_INDEX_CAP];
    uint32_t ordered[TRACK_INDEX_CAP];
    unsigned char matches[(TRACK_INDEX_CAP+7)/8];
    TrackSource ordered_source;
    uint32_t ordered_base;
    TrackSort ordered_sort;
    bool ordered_valid;
    TrackRecord batch[TRACK_BATCH];
    TrackJob job;
    Worker *worker;
    EventQueue *events;
    uint64_t ticket;
    atomic_uint phase; /* 0 UI-owned, 1 worker-owned, 2 immutable completion. */
} TrackFilter;
bool track_utf8(const char *, size_t);
bool track_record_valid(const TrackRecord *);
bool track_matches(const TrackRecord *, const char *query);
TrackResult track_filter_init(TrackFilter *, Worker *, EventQueue *);
TrackResult track_filter_start(TrackFilter *, const TrackJob *);
void track_filter_cancel(TrackFilter *);
const TrackJob *track_filter_done(TrackFilter *);
void track_filter_release(TrackFilter *);
TrackResult track_filter_clear(TrackFilter *); /* Idle only; retire private index/workspace. */
/* Same bounded kernel used by the worker and Release benchmark. Never UI owner.
 * Caller exclusively owns Filter and immutable Source throughout execution. */
TrackResult track_filter_execute(TrackFilter *, const CancelToken *);
#endif
