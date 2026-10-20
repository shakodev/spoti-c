#include "ui/app_artwork_host.h"
#include "support/test_check.h"
#include <string.h>

static AppArtworkHost host;
static UiRenderer renderer; /* No device: lifecycle tests never request an image. */
static _Alignas(16) unsigned char storage[MEMORY_BUDGET_CORE], tiny_storage[512];
static HANDLE started, release;
static _Atomic unsigned completions;
static void gated_run(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; (void)cancel; (void)context;
    CHECK(SetEvent(started));
    CHECK(WaitForSingleObject(release,5000) == WAIT_OBJECT_0);
}
static void completed(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; (void)context;
    CHECK(outcome == WORKER_CANCELLED);
    atomic_fetch_add(&completions,1);
}
int main(void)
{
    Metrics metrics; MetricsArena binding = {0}; Arena core;
    EventQueue *events = NULL;
    DWORD before, after;
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    CHECK(metrics_init(&metrics) && arena_init(&core,storage,sizeof(storage)));
    CHECK(metrics_attach_arena(&metrics,&core,&binding));
    CHECK(event_queue_create(&core,8,&events) == CORE_OK);
    ArenaMark base = arena_mark(&core);
    CHECK(!app_artwork_open(NULL,&core,&renderer,events,&metrics));
    CHECK(!app_artwork_open(&host,&core,NULL,events,&metrics));
    CHECK(app_artwork_close(&host,0));
    CHECK(app_artwork_open(&host,&core,&renderer,events,&metrics));
    CHECK(app_artwork_service(&host) != NULL && host.staging_arena.used == MEMORY_BUDGET_ARTWORK_STAGING);
    CHECK(metrics_snapshot(&metrics).arena_capacity == MEMORY_BUDGET_CORE+MEMORY_BUDGET_ARTWORK_STAGING);
    CHECK(WaitForSingleObject(worker_idle_handle(host.worker),5000) == WAIT_OBJECT_0);
    CHECK(worker_stats(host.worker).accepted == 0 && artwork_stats(host.art).submitted == 0);
    CHECK(!app_artwork_open(&host,&core,&renderer,events,&metrics));
    CHECK(app_artwork_close(&host,5000) && app_artwork_service(&host) == NULL);
    CHECK(core.used == base.used && metrics_snapshot(&metrics).arena_capacity == MEMORY_BUDGET_CORE);
    CHECK(app_artwork_close(&host,0));

    CHECK(app_artwork_open(&host,&core,&renderer,events,NULL));
    unsigned char *later = arena_alloc(&core,64,16);
    CHECK(later); memset(later,0xa5,64); size_t used = core.used;
    CHECK(app_artwork_close(&host,5000));
    CHECK(core.used == used);
    for (unsigned i = 0; i < 64; ++i) CHECK(later[i] == 0xa5);
    CHECK(arena_rewind(&core,base));

    Arena tiny;
    CHECK(arena_init(&tiny,tiny_storage,sizeof(tiny_storage)));
    CHECK(!app_artwork_open(&host,&tiny,&renderer,events,&metrics));
    CHECK(app_artwork_close(&host,5000) && tiny.used == 0);
    CHECK(metrics_snapshot(&metrics).arena_capacity == MEMORY_BUDGET_CORE);
    arena_dispose(&tiny);

    CHECK(app_artwork_open(&host,&core,&renderer,events,&metrics));
    started = CreateEventW(NULL,TRUE,FALSE,NULL); release = CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(started && release);
    WorkerJob job = {.run=gated_run,.complete=completed,.deadline=CORE_CLOCK_NEVER,.priority=WORKER_ART};
    uint64_t ticket;
    CHECK(worker_submit(host.worker,&job,&ticket) == CORE_OK);
    CHECK(WaitForSingleObject(started,5000) == WAIT_OBJECT_0);
    CHECK(!app_artwork_close(&host,0)); /* The injected callback still owns worker storage. */
    CHECK(app_artwork_service(&host) == NULL && host.staging_ready && host.worker);
    CHECK(SetEvent(release) && app_artwork_close(&host,5000));
    CHECK(atomic_load(&completions) == 1 && core.used == base.used);
    CHECK(CloseHandle(started) && CloseHandle(release));
    CHECK(event_queue_close(events) == CORE_OK);
    Event discarded[8]; size_t count;
    CHECK(event_drain(events,discarded,8,&count) == CORE_CLOSED && count == 0);
    CHECK(event_queue_destroy(events) == CORE_OK);
    arena_dispose(&core);
    CHECK(metrics_balanced(&metrics));
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before == after);
    puts("artwork host: deferred storage, arena budgets, partial failure, cancellation and joined ownership passed without network/GPU");
    return 0;
}
