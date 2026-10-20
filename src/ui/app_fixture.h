#ifndef SPOTIC_UI_APP_FIXTURE_H
#define SPOTIC_UI_APP_FIXTURE_H
#include "app.h"
#include "core/memory_budget.h"
#include "screens/playlist.h"
#include "track_disk.h"

#define APP_FIXTURE_EVENT (0x3000u)
typedef struct AppFixtureSlot {
    AppRequest request;
    const CollectionPage *page;
    const AppRootlistPage *rootlist;
    const CatalogPage *catalog_page;
    const LyricsDocument *lyrics;
    CatalogResult catalog_result;
    LyricsResult lyrics_result;
    char artwork[512];
    CollectionResult result;
    uint64_t ticket;
    AppAuth auth;
    TrackSource source;
    bool authentication, playlist;
    atomic_uint phase;
} AppFixtureSlot;
/* Explicit offline host. Runs existing collection/catalog/lyrics services on a serialized worker;
 * caller supplies raw-body fixtures in a directory, never credentials. This
 * is not a live Spotify transport. API storage uses the existing shared model
 * and API-page budgets; catalog/lyrics controllers occupy App's unused UI-state
 * reservation. Initialize App before submitting work and keep it live through
 * fixture_stop, including after app_dispose. */
typedef struct AppFixture {
    _Alignas(16) unsigned char service_storage[COLLECTION_MODEL_BUDGET];
    _Alignas(16) unsigned char pool_storage[MEMORY_BUDGET_API_PAGES];
    CollectionService *service;
    AppRootlistStore *rootlists;
    CatalogService *catalog;
    LyricsService *lyrics;
    JsonPagePool *pool;
    Worker *worker;
    EventQueue *events;
    App *app;
    void *player_context; /* Optional caller-owned existing playback service. */
    AppFixtureSlot slots[4];
    wchar_t directory[256];
    uint64_t generation;
    bool stopping;
    atomic_uint retirement_phase;
    atomic_uint_fast64_t retire_generation;
    uint64_t retired;
    Playlist playlist;
    TrackFilter filter;
    TrackDisk disk;
    PlaylistQueue last_queue;
    wchar_t disk_path[MAX_PATH];
} AppFixture;
_Static_assert(sizeof(AppFixture)-MEMORY_BUDGET_API_PAGES<=512u*1024u,"shared fixture collection/playlist models");
CollectionResult app_fixture_playlist_open(AppFixture *, AppFixtureSlot *, const CancelToken *);
bool app_fixture_playlist_close(AppFixture *); /* Only after worker join, or on that worker. */
void app_fixture_catalog_run(AppFixture *, AppFixtureSlot *, const CancelToken *);
void app_fixture_sidebar_run(AppFixture *, AppFixtureSlot *, const CancelToken *);
void app_fixture_reset(AppFixture *, uint64_t);
void app_fixture_retire(void *, uint64_t);
void app_fixture_retirement_pump(AppFixture *);
AppResult app_fixture_init(AppFixture *, Arena *core, EventQueue *, App *, const wchar_t *directory);
AppServices app_fixture_services(AppFixture *);
void app_fixture_pump(AppFixture *); /* UI prep: consume published immutable pages. */
bool app_fixture_stop(AppFixture *); /* Stop/join before buffers leave scope. */
#endif
