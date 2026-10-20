#ifndef SPOTIC_PLAYLIST_SCREEN_H
#define SPOTIC_PLAYLIST_SCREEN_H
#include "ui/track_filter.h"
#include "ui/widgets.h"
#include "api/collection_api.h"
#include "audio/player.h"
struct App;
typedef struct TrackRow {
    uint32_t ordinal, duration_ms, flags;
    uint16_t title, artist, album, time;
} TrackRow;
typedef struct PlaylistQueue {
    PlayerTransfer transfer;
    uint32_t ordinals[PLAYER_QUEUE_CAP];
    char uri[192], revision[64];
    bool more;
} PlaylistQueue;
typedef struct Playlist {
    TrackFilter *filter;
    TrackSource source;
    uint32_t map[TRACK_INDEX_CAP], count, base, missing;
    TrackRow rows[TRACK_BATCH];
    char strings[24u*1024u];
    uint32_t row_count, string_bytes;
    char query[TRACK_QUERY_BYTES], uri[192], summary[192];
    uint64_t route, generation, cache_generation, selected, play_key, play_generation, stale_jobs, first_playable;
    TrackCoverage coverage;
    TrackSort sort;
    TrackResult result;
    bool active, ready, pending, need_filter, selected_visible, selected_playable;
    uint16_t surrogate;
    uint32_t rendered_rows;
} Playlist;
_Static_assert(sizeof(Playlist)+sizeof(TrackFilter)+COLLECTION_MODEL_BUDGET<=512u*1024u,
    "playlist index/maps/filter/cache plus shared collection model budget");
void playlist_init(Playlist *, TrackFilter *);
TrackResult playlist_route(Playlist *, const char *uri, uint64_t route);
void playlist_close(Playlist *);
TrackResult playlist_source(Playlist *, const TrackSource *, uint64_t route);
TrackResult playlist_query(Playlist *, const char *, TrackSort);
TrackResult playlist_window(Playlist *, bool next);
TrackResult playlist_prepare(Playlist *, UiVirtualList *, float viewport);
const TrackRow *playlist_row(const Playlist *, uint32_t ordinal);
TrackResult playlist_select(Playlist *, uint64_t key, bool play);
uint64_t playlist_play_target(const Playlist *);
TrackResult playlist_queue(const Playlist *, uint64_t key, PlaylistQueue *);
TrackResult playlist_take_play(Playlist *, PlaylistQueue *);
bool playlist_index(void *, uint64_t, uint32_t *);
uint64_t playlist_key(void *, uint32_t);
void app_playlist(struct App *);
void app_tracklist(struct App *);
bool playlist_text_message(struct App *, void *hwnd, uint32_t message, uintptr_t wp, intptr_t lp);
#endif
