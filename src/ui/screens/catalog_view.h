#ifndef SPOTIC_CATALOG_VIEW_H
#define SPOTIC_CATALOG_VIEW_H
#include "ui/app_result.h"
#include "api/catalog_api.h"
#include "api/lyrics.h"
struct App;
struct AppEntity;
struct AppRequest;
#define APP_QUERY_BYTES 96u
#define APP_SEARCH_DELAY 250u
#define APP_CATALOG_ROW 56u
#define APP_LYRICS_HOLD 5000u
typedef struct CatalogView {
    const CatalogPage *page; /* One owned route reference, never a growing list. */
    LyricsTimeline timeline;
    LyricsView lyric;
    uint64_t debounce, hold, now;
    uint8_t track_id[16];
    CatalogResult result;
    LyricsResult lyrics_result;
    uint32_t rendered;
    uint16_t surrogate;
    char title[192], subtitle[192], artwork[512];
    uint32_t artwork_width, artwork_height;
} CatalogView;
bool app_is_catalog(const struct App *);
void app_catalog_clear(struct App *);
AppResult app_catalog_request(struct App *, uint32_t cursor);
AppResult app_catalog_fetch(struct App *, uint32_t cursor);
void app_catalog_prepare(struct App *, uint64_t now);
AppResult app_catalog_deliver(struct App *, struct AppRequest, CatalogResult, const CatalogPage *);
AppResult app_lyrics_deliver(struct App *, struct AppRequest, LyricsResult, const LyricsDocument *);
AppResult app_catalog_entity(const CatalogItem *, struct AppEntity *);
void app_catalog_rows(struct App *);
void app_catalog_header(struct App *);
void app_catalog_status(struct App *);
uint64_t app_catalog_key(void *, uint32_t);
bool app_catalog_index(void *, uint64_t, uint32_t *);
AppResult app_catalog_activate(struct App *, uint32_t);
uint32_t app_catalog_play_index(const struct App *);
float app_catalog_viewport(const struct App *);
void app_search(struct App *);
void app_album(struct App *);
void app_artist(struct App *);
void app_lyrics(struct App *);
bool app_search_text_message(struct App *, void *, uint32_t, uintptr_t, intptr_t);
void app_lyrics_hold(struct App *);
#endif
