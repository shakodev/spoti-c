#ifndef SPOTIC_APP_ARTWORK_HOST_H
#define SPOTIC_APP_ARTWORK_HOST_H
#include "artwork.h"

typedef struct AppArtworkHost {
    Arena staging_arena;
    MetricsArena staging_metrics;
    Arena *core;
    ArenaMark core_mark;
    size_t core_end;
    Worker *worker;
    Artwork *art;
    DWORD owner;
    bool staging_ready, ready, closing;
    unsigned char alignment_padding[9];
    _Alignas(16) unsigned char staging[MEMORY_BUDGET_ARTWORK_STAGING];
} AppArtworkHost;
_Static_assert(ART_DECODE_BYTES + ART_UPLOAD_BYTES == MEMORY_BUDGET_ARTWORK_STAGING,
    "artwork staging uses the existing budget");

/* Zero-initialized, static caller storage; owner UI thread only, no live copies.
 * Open after first useful present, never from layout/render. It does no network
 * or GPU work. core/renderer/events/metrics must outlive successful close.
 * Failed open may retain a timed-out worker; always close before dependencies. */
bool app_artwork_open(AppArtworkHost *host, Arena *core, UiRenderer *renderer,
    EventQueue *events, Metrics *metrics);
Artwork *app_artwork_service(AppArtworkHost *host);
/* False preserves any live resources for retry. No callback survives true.
 * Shared core storage is reclaimed only when still the last allocation suffix. */
bool app_artwork_close(AppArtworkHost *host, DWORD timeout_ms);
#endif
