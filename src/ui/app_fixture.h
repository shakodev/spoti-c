#ifndef SPOTIC_UI_APP_FIXTURE_H
#define SPOTIC_UI_APP_FIXTURE_H
#include "app.h"
#include "core/memory_budget.h"

#define APP_FIXTURE_EVENT (0x3000u)
typedef struct AppFixtureSlot {
    AppRequest request;
    const CollectionPage *page;
    CollectionResult result;
    uint64_t ticket;
    AppAuth auth;
    bool authentication;
    atomic_uint phase;
} AppFixtureSlot;
/* Explicit offline host. Runs real CollectionService on a serialized worker;
 * caller supplies raw-body fixtures in a directory, never credentials. This
 * is not a live Spotify transport. API storage uses the existing shared model
 * and API-page budgets, separately from App's <=256 KiB state. */
typedef struct AppFixture {
    _Alignas(16) unsigned char service_storage[COLLECTION_MODEL_BUDGET];
    _Alignas(16) unsigned char pool_storage[MEMORY_BUDGET_API_PAGES];
    CollectionService *service;
    JsonPagePool *pool;
    Worker *worker;
    EventQueue *events;
    App *app;
    void *player_context; /* Optional caller-owned existing playback service. */
    AppFixtureSlot slots[4];
    wchar_t directory[256];
    uint64_t generation;
    bool stopping;
} AppFixture;
AppResult app_fixture_init(AppFixture *, Arena *core, EventQueue *, App *, const wchar_t *directory);
AppServices app_fixture_services(AppFixture *);
void app_fixture_pump(AppFixture *); /* UI prep: consume published immutable pages. */
bool app_fixture_stop(AppFixture *); /* Stop/join before buffers leave scope. */
#endif
