#include "ui/live_internal.h"
#include "ui/app_artwork_host.h"
#include "ui/startup.h"
#include "support/test_check.h"
#include "core/thread.h"
#include <string.h>
#include <wchar.h>

static App app;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES], core_memory[MEMORY_BUDGET_CORE];
static Startup startup;
static AppArtworkHost artwork_host;
static UiRenderer renderer; /* Lifecycle-only host: no image/GPU operation. */
static HANDLE art_started, art_release;
static _Atomic unsigned art_completions;
static void art_run(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; (void)cancel; (void)context;
    CHECK(SetEvent(art_started));
    CHECK(WaitForSingleObject(art_release,10000)==WAIT_OBJECT_0);
}
static void art_done(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; CHECK(outcome==WORKER_CANCELLED);
    Event notification={.type=UI_DIRTY_STATE};
    CHECK(event_post(context,&notification)==CORE_OK);
    atomic_fetch_add_explicit(&art_completions,1,memory_order_release);
}
static void drain(EventQueue *queue)
{
    Event events[32]; size_t count; CoreResult result=event_drain(queue,events,32,&count);
    CHECK(result==CORE_OK || result==CORE_CLOSED);
}
static void settle(AppLive *host, EventQueue *queue)
{
    for (unsigned i=0;i<4;++i) {
        CHECK(WaitForSingleObject(worker_idle_handle(host->worker),10000)==WAIT_OBJECT_0);
        app_live_pump(host); drain(queue);
    }
}
static void blocker(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; (void)context;
    CHECK(WaitForSingleObject(worker_cancel_handle(cancel),10000)==WAIT_OBJECT_0);
    CHECK(worker_cancelled(cancel));
}
static void blocked_done(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; (void)outcome; *(bool *)context=true;
}
typedef struct OwnerProbe { AppLive *host; bool done; } OwnerProbe;
static void disk_owner(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; CHECK(!worker_cancelled(cancel)); OwnerProbe *probe=context;
    /* A Windows queue does not turn a registered service owner into the UI. */
    CHECK(core_thread_background_owner());
    CHECK(IsGUIThread(TRUE)); CHECK(!core_thread_is_gui());
    TrackDisk disk={.file=INVALID_HANDLE_VALUE};
    CHECK(track_disk_create(&disk,probe->host->index_path,"fixture:owner","revision",true)==TRACK_OK);
    TrackRecord row={.id={1},.flags=TRACK_ID|TRACK_PLAYABLE}; strcpy_s(row.title,sizeof(row.title),"Fixture track");
    CHECK(track_disk_append(&disk,&row,1)==TRACK_OK && track_disk_finish(&disk)==TRACK_OK);
    CHECK(track_disk_close(&disk)); CHECK(DeleteFileW(probe->host->index_path));
}
static void disk_owner_done(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; CHECK(outcome==WORKER_DONE); ((OwnerProbe *)context)->done=true;
}
int main(void)
{
    CHECK(!core_thread_background_owner());
    CHECK(IsGUIThread(TRUE) && core_thread_is_gui());
    TrackDisk blocked={.file=INVALID_HANDLE_VALUE};
    CHECK(track_disk_create(&blocked,L"unused","fixture:ui","revision",true)==TRACK_INVALID);
    CHECK(SetEnvironmentVariableA("SPOTIC_OAUTH_CLIENT_ID","fixture"));
    CHECK(SetEnvironmentVariableA("SPOTIC_OAUTH_REDIRECT_URI","invalid"));
    CHECK(SetEnvironmentVariableA("SPOTIC_OAUTH_SCOPES","streaming"));
    wchar_t temp[MAX_PATH], directory[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH,temp));
    CHECK(GetTempFileNameW(temp,L"spl",0,directory)); CHECK(DeleteFileW(directory));
    Metrics metrics; MetricsArena binding={0}; Arena core;
    CHECK(metrics_init(&metrics) && arena_init(&core,core_memory,sizeof(core_memory)));
    CHECK(metrics_attach_arena(&metrics,&core,&binding));
    EventQueue *queue; CHECK(event_queue_create(&core,32,&queue)==CORE_OK);
    wchar_t state_path[240]; CHECK(swprintf_s(state_path,240,L"%ls/shell.state",directory)>0);
    startup_begin(&startup,state_path);
    CHECK(startup_writer_start(&startup,&core)==STATE_CACHE_OK);
    UiScheduler scheduler; ui_scheduler_init(&scheduler);
    AppServices empty={0}; CHECK(app_init(&app,&empty,&scheduler,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    AppLive *host; CHECK(app_live_open(&core,queue,&app,directory,&host)==APP_OK);
    app.services=app_live_services(host); settle(host,queue);
    CHECK(!app.model.logged_in && !app.model.auth_pending && !app.model.notice[0]);
    ArenaMark live_prefix=arena_mark(&core);
    CHECK(app_artwork_open(&artwork_host,&core,&renderer,queue,&metrics));
    app.services.artwork=app_artwork_service(&artwork_host);
    CHECK(app.services.artwork && artwork_navigate(app.services.artwork,app.model.generation)==ART_OK);
    CHECK(startup.worker!=host->worker && artwork_host.worker!=host->worker &&
        artwork_host.worker!=startup.worker && core.high_water<=MEMORY_BUDGET_CORE);
    CHECK(metrics_snapshot(&metrics).arena_capacity==MEMORY_BUDGET_CORE+MEMORY_BUDGET_ARTWORK_STAGING);
    /* A failed playlist delivery must replace Loading even without a source. */
    app.model.request=(AppRequest){.screen=APP_PLAYLIST,.generation=app.model.generation,.serial=7};
    app.model.pending=true; app.model.status=APP_LOADING;
    CHECK(app.services.request(app.services.context,&app.model.request)==APP_PENDING); settle(host,queue);
    CHECK(!app.model.pending && app.model.status==APP_ERROR && app.model.last_result==APP_FAILED);
    app.model.last_result=APP_OK;
    OwnerProbe probe={.host=host}; uint64_t owner_ticket;
    WorkerJob owner_job={.run=disk_owner,.complete=disk_owner_done,.context=&probe,.deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK};
    CHECK(worker_submit(host->worker,&owner_job,&owner_ticket)==CORE_OK); settle(host,queue); CHECK(probe.done);
    uint64_t waits=worker_stats(host->worker).waits;
    CHECK(WaitForSingleObject(worker_idle_handle(host->worker),0)==WAIT_OBJECT_0);
    CHECK(worker_stats(host->worker).waits==waits); /* No jobs scheduled by an idle pump. */
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SIGN_IN})==APP_OK); (void)app_update(&app,GetTickCount64());
    CHECK(app.model.auth_pending); settle(host,queue);
    CHECK(!app.model.auth_pending && !app.model.logged_in && strstr(app.model.notice,"OAuth"));
    /* Queue login behind a blocked owner, then cancel it before it can open a browser. */
    bool completed=false; WorkerJob job={.run=blocker,.complete=blocked_done,.context=&completed,
        .deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK}; uint64_t ticket;
    CHECK(worker_submit(host->worker,&job,&ticket)==CORE_OK);
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SIGN_IN})==APP_OK); (void)app_update(&app,GetTickCount64());
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SIGN_OUT})==APP_OK); (void)app_update(&app,GetTickCount64());
    CHECK(worker_cancel(host->worker,ticket)); settle(host,queue);
    CHECK(completed && !app.model.logged_in && !app.model.auth_pending && !app.model.notice[0]);
    CredentialStore store={0}; CredentialRecord record;
    CHECK(credential_record_init(&record,"fixture")==CREDENTIAL_OK);
    strcpy_s(record.refresh_token,sizeof(record.refresh_token),"synthetic-refresh");
    CHECK(credential_store_save(&store,host->credentials_path,&record)==CREDENTIAL_OK);
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SIGN_OUT})==APP_OK); (void)app_update(&app,GetTickCount64()); settle(host,queue);
    CHECK(GetFileAttributesW(host->credentials_path)==INVALID_FILE_ATTRIBUTES && credential_store_balanced(&store));
    credential_record_clear(&record);
    /* Mirror main's teardown with art work still retaining the shared core.
     * The state writer must complete independently; an art timeout must retain
     * its suffix until the callback can publish into the still-live UI queue. */
    art_started=CreateEventW(NULL,TRUE,FALSE,NULL); art_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(art_started && art_release);
    WorkerJob art_job={.run=art_run,.complete=art_done,.context=queue,
        .deadline=CORE_CLOCK_NEVER,.priority=WORKER_ART}; uint64_t art_ticket;
    CHECK(worker_submit(artwork_host.worker,&art_job,&art_ticket)==CORE_OK);
    CHECK(WaitForSingleObject(art_started,10000)==WAIT_OBJECT_0);
    StateCache state, restored; state_cache_defaults(&state,STARTUP_EPOCH); state.volume=375;
    CHECK(startup_save(&startup,&state)==STATE_CACHE_OK);
    CHECK(startup_writer_close(&startup,10000)==STATE_CACHE_OK && !startup.worker);
    CHECK(state_cache_load(state_path,STARTUP_EPOCH,&restored)==STATE_CACHE_OK && restored.volume==375);
    size_t occupied=core.used;
    CHECK(!app_artwork_close(&artwork_host,0));
    CHECK(core.used==occupied && artwork_host.staging_ready && artwork_host.worker);
    CHECK(SetEvent(art_release) && app_artwork_close(&artwork_host,10000));
    CHECK(atomic_load_explicit(&art_completions,memory_order_acquire)==1);
    CHECK(core.used==live_prefix.used && app_artwork_service(&artwork_host)==NULL);
    app.services.artwork=NULL; drain(queue);
    CHECK(CloseHandle(art_started) && CloseHandle(art_release));
    /* Reclaiming the art suffix must not poison a neighboring live worker. */
    probe.done=false;
    CHECK(worker_submit(host->worker,&owner_job,&owner_ticket)==CORE_OK); settle(host,queue); CHECK(probe.done);
    CHECK(DeleteFileW(state_path));
    wchar_t cache_path[230]; wcscpy_s(cache_path,230,host->cache_path);
    app_dispose(&app); CHECK(app_live_close(host));
    CHECK(RemoveDirectoryW(cache_path)); CHECK(RemoveDirectoryW(directory));
    CHECK(event_queue_close(queue)==CORE_OK); drain(queue); CHECK(event_queue_destroy(queue)==CORE_OK);
    size_t high_water=core.high_water; arena_dispose(&core); CHECK(metrics_balanced(&metrics));
    printf("Live host + state/art workers: core high-water %zu/%zu bytes; cancellation and teardown balanced.\n",
        high_water,(size_t)MEMORY_BUDGET_CORE);
    puts("Live host restore, login failure, cancelled login, logout and teardown passed."); return 0;
}
