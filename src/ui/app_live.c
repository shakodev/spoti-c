#include "live_internal.h"
#include <string.h>
#include <wchar.h>

static void reset(AppLive *h, uint64_t generation)
{
    if (generation<=h->generation) { return; }
    (void)collection_reset(h->collections,generation);
    if (h->catalog) { (void)catalog_reset(h->catalog,generation); }
    if (h->lyrics) { (void)lyrics_reset(h->lyrics,generation); }
    h->generation=generation;
}
static bool initialize(AppLive *h)
{
    if (h->initialized) { return true; }
    if ((!CreateDirectoryW(h->directory,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) ||
        (!CreateDirectoryW(h->cache_path,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS)) { return false; }
    h->initialized=true; return true;
}
static void run(const Event *event, const CancelToken *cancel, void *context)
{
    AppLive *h=context; LiveSlot *slot=&h->slots[event->id];
    if (!initialize(h)) { strcpy_s(slot->message,sizeof(slot->message),"Could not open the private application data directory."); return; }
    if (slot->retirement) { reset(h,slot->request.generation); return; }
    if (slot->auth) {
        if (slot->logout) {
            slot->success=live_account_close(h,true);
            if (!slot->success) { strcpy_s(slot->message,sizeof(slot->message),"Could not fully remove saved credentials. Please retry logout."); }
        } else {
            slot->success=slot->restore ? live_account_restore(h,slot,cancel) : live_account(h,slot,cancel);
            if (!slot->success) { (void)live_account_close(h,false); }
            else if (!h->player) { strcpy_s(slot->message,sizeof(slot->message),"Signed in. Audio could not start; check your output device and sign in again."); }
        }
        return;
    }
    reset(h,slot->request.generation);
    if (!h->authenticated) { slot->result=COLLECTION_AUTH; return; }
    live_request(h,slot,cancel);
}
static void complete(const Event *event, WorkerOutcome outcome, void *context)
{
    AppLive *h=context; LiveSlot *slot=&h->slots[event->id];
    if (outcome!=WORKER_DONE) { slot->result=COLLECTION_CANCELLED; slot->catalog_result=CATALOG_CANCELLED; slot->success=false; }
    atomic_store_explicit(&slot->phase,2,memory_order_release); live_notify(h);
}
static AppResult enqueue(AppLive *h, AppRequest request, bool auth, bool logout, bool restore, bool retire, JsonString artwork)
{
    if (!h || h->stopping) { return APP_UNAVAILABLE; }
    for (uint32_t i=0;i<(auth ? LIVE_SLOTS : LIVE_SLOTS-1);++i) {
        LiveSlot *slot=&h->slots[i];
        if (atomic_load_explicit(&slot->phase,memory_order_acquire)) { continue; }
        slot->request=request; slot->auth=auth; slot->logout=logout; slot->restore=restore; slot->retirement=retire;
        if (artwork.bytes>=sizeof(slot->artwork) || (artwork.bytes && !artwork.data)) { return APP_INVALID; }
        if (artwork.bytes) { memcpy(slot->artwork,artwork.data,artwork.bytes); } slot->artwork[artwork.bytes]=0;
        slot->page=NULL; slot->rootlist=NULL; slot->catalog_page=NULL; slot->lyrics=NULL; slot->lyrics_result=LYRICS_ERROR; slot->source=(TrackSource){0};
        slot->success=false; slot->message[0]=0; slot->result=COLLECTION_ERROR; slot->catalog_result=CATALOG_ERROR;
        atomic_store_explicit(&slot->phase,1,memory_order_release);
        WorkerJob job={.request={.id=i},.run=run,.complete=complete,.context=h,
            .deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK};
        if (worker_submit(h->worker,&job,&slot->ticket)==CORE_OK) { return APP_PENDING; }
        atomic_store_explicit(&slot->phase,0,memory_order_release); return APP_FULL;
    }
    return APP_FULL;
}
static AppResult request(void *context, const AppRequest *request)
{
    return enqueue(context,*request,false,false,false,false,(JsonString){0});
}
static AppResult catalog(void *context, const AppRequest *request, JsonString artwork)
{
    return enqueue(context,*request,false,false,false,false,artwork);
}
static void cancel(void *context, const AppRequest *request)
{
    AppLive *h=context;
    for (unsigned i=0;i<LIVE_SLOTS;++i) {
        LiveSlot *slot=&h->slots[i];
        if (atomic_load_explicit(&slot->phase,memory_order_acquire)==1 && !slot->auth && !slot->retirement &&
            slot->request.generation==request->generation && slot->request.serial==request->serial) { (void)worker_cancel(h->worker,slot->ticket); }
    }
}
static void retire(void *context, uint64_t generation)
{
    AppLive *h=context; atomic_store_explicit(&h->retirement,generation,memory_order_release);
    /* Coalesced retirement is admitted by the UI pump after any running job. */
}
static AppResult auth(void *context, AppAuth action)
{
    AppLive *h=context;
    for (unsigned i=0;i<LIVE_SLOTS;++i) {
        if (atomic_load_explicit(&h->slots[i].phase,memory_order_acquire)==1) { (void)worker_cancel(h->worker,h->slots[i].ticket); }
    }
    AppRequest request={.serial=h->app->model.auth_serial};
    strcpy_s(h->app->model.notice,sizeof(h->app->model.notice),action==APP_LOGIN ? "Complete sign-in in your browser..." : "Signing out...");
    AppResult result=enqueue(h,request,true,action==APP_LOGOUT,false,false,(JsonString){0});
    if (result==APP_FULL) {
        h->pending_auth=request; h->auth_waiting=true; h->logout_waiting=action==APP_LOGOUT; return APP_PENDING;
    }
    h->auth_waiting=false; return result;
}
static PlayerResult submit(void *context, const PlayerCommand *command)
{
    AppLive *h=context; PlayerResult result;
    AcquireSRWLockShared(&h->player_lock);
    result=h->player ? headless_submit(h->player,command) : PLAYER_BAD_STATE;
    ReleaseSRWLockShared(&h->player_lock); return result;
}
static PlayerSnapshot snapshot(void *context)
{
    AppLive *h=context; PlayerSnapshot result={.volume=1};
    AcquireSRWLockShared(&h->player_lock);
    if (h->player) { result=headless_snapshot(h->player); }
    ReleaseSRWLockShared(&h->player_lock); return result;
}
static bool track_info(void *context,const uint8_t id[16],MediaDisplay *out)
{
    AppLive *h=context; bool result=false;
    AcquireSRWLockShared(&h->player_lock);
    if (h->player) { result=headless_track_info(h->player,id,out); }
    ReleaseSRWLockShared(&h->player_lock); return result;
}
static bool queue_snapshot(void *context,PlayerQueueSnapshot *out)
{
    AppLive *h=context; bool result=false;
    AcquireSRWLockShared(&h->player_lock);
    if (h->player) { result=headless_queue_snapshot(h->player,out); }
    ReleaseSRWLockShared(&h->player_lock); return result;
}
static PlayerResult refresh(void *context)
{
    AppLive *h=context; PlayerResult result=PLAYER_OK;
    AcquireSRWLockShared(&h->player_lock);
    if (h->player) { result=headless_refresh_clock(h->player); }
    ReleaseSRWLockShared(&h->player_lock); return result;
}
static PlayerResult queue(void *context, const PlaylistQueue *queue)
{
    return submit(context,&(PlayerCommand){.type=PLAYER_TRANSFER,.transfer=&queue->transfer});
}
AppServices app_live_services(AppLive *h)
{
    return (AppServices){.context=h,.request=request,.sidebar=request,.catalog=catalog,.cancel=cancel,.retire=retire,.auth=auth,
        .submit=submit,.snapshot=snapshot,.track_info=track_info,.queue_snapshot=queue_snapshot,.refresh_clock=refresh,.playlist=&h->playlist,.playlist_queue=queue};
}
static bool environment(const char *name, char *out, DWORD cap, const char *fallback)
{
    SetLastError(ERROR_SUCCESS); DWORD n=GetEnvironmentVariableA(name,out,cap);
    if (!n && GetLastError()==ERROR_ENVVAR_NOT_FOUND) { return strcpy_s(out,cap,fallback)==0; }
    return n && n<cap;
}
AppResult app_live_open(Arena *core, EventQueue *events, App *app, const wchar_t *directory, AppLive **output)
{
    if (!output) { return APP_INVALID; } *output=NULL;
    if (!core || !events || !app || !directory || wcslen(directory)>190) { return APP_INVALID; }
    AppLive *h=VirtualAlloc(NULL,sizeof(*h),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if (!h) { return APP_FULL; }
    h->app=app; h->events=events; h->disk.file=INVALID_HANDLE_VALUE;
    InitializeSRWLock(&h->player_lock);
    for (unsigned i=0;i<LIVE_SLOTS;++i) { atomic_init(&h->slots[i].phase,0); }
    atomic_init(&h->retirement,0);
    wcscpy_s(h->directory,230,directory);
    (void)swprintf_s(h->credentials_path,230,L"%ls/credentials.dat",directory);
    (void)swprintf_s(h->cache_path,230,L"%ls/audio",directory);
    (void)swprintf_s(h->index_path,230,L"%ls/playlist.index",directory);
    bool ok=environment("SPOTIC_OAUTH_CLIENT_ID",h->client_id,sizeof(h->client_id),"65b708073fc0480ea92a077233ca87bd") &&
        environment("SPOTIC_OAUTH_REDIRECT_URI",h->redirect,sizeof(h->redirect),"http://127.0.0.1/login") &&
        environment("SPOTIC_OAUTH_SCOPES",h->scopes,sizeof(h->scopes),"streaming user-read-private user-read-email user-personalized playlist-read-private playlist-read-collaborative user-library-read") &&
        environment("SPOTIC_COUNTRY",h->country,sizeof(h->country),"NL") &&
        environment("SPOTIC_TIME_ZONE",h->timezone,sizeof(h->timezone),"Europe/Amsterdam");
    ok=ok && arena_init(&h->http_arena,h->http_storage,sizeof(h->http_storage)) &&
        arena_init(&h->secret_arena,h->secret_storage,sizeof(h->secret_storage)) &&
        arena_init(&h->token_arena,h->token_storage,sizeof(h->token_storage)) &&
        json_page_pool_init(h->pool_storage,sizeof(h->pool_storage),&h->pool)==JSON_OK &&
        collection_service_init(h->collection_storage,sizeof(h->collection_storage),h->pool,&h->collections)==COLLECTION_OK;
    if (ok) { h->rootlists=app_rootlist_store(h->collection_storage,sizeof(h->collection_storage)); ok=h->rootlists!=NULL; }
    if (!ok || worker_start(core,8,(CoreClock){0},&h->worker)!=CORE_OK) {
        arena_dispose(&h->http_arena); arena_dispose(&h->secret_arena); arena_dispose(&h->token_arena);
        VirtualFree(h,0,MEM_RELEASE); return APP_FAILED;
    }
    if (track_filter_init(&h->filter,h->worker,events)!=TRACK_OK) { (void)app_live_close(h); return APP_FAILED; }
    playlist_init(&h->playlist,&h->filter);
    *output=h;
    (void)enqueue(h,(AppRequest){.serial=app->model.auth_serial},true,false,true,false,(JsonString){0}); return APP_OK;
}
void app_live_pump(AppLive *h)
{
    if (!h || h->stopping) { return; }
    bool busy=false;
    for (unsigned i=0;i<LIVE_SLOTS;++i) {
        LiveSlot *slot=&h->slots[i]; unsigned phase=atomic_load_explicit(&slot->phase,memory_order_acquire);
        if (phase==1) { busy=true; continue; }
        if (phase!=2) { continue; }
        if (slot->auth && slot->request.serial==h->app->model.auth_serial) {
            h->app->model.auth_pending=false;
            bool had_session=h->app->model.logged_in;
            if (had_session) {
                /* Reauthentication replaces the live account/player. Retire
                 * its pages and snapshot generation before accepting either
                 * a replacement session or a failed login. */
                (void)app_session(h->app,false,h->app->model.online,GetTickCount64());
            }
            if (slot->success) {
                (void)app_session(h->app,!slot->logout,true,GetTickCount64());
                h->app->model.last_result=APP_OK;
            } else if (had_session) { h->app->model.last_result=APP_FAILED; }
            strcpy_s(h->app->model.notice,sizeof(h->app->model.notice),slot->message);
        } else if (!slot->auth && !slot->retirement) {
            if (slot->request.sidebar) {
                (void)app_sidebar_deliver(h->app,slot->request,slot->result,slot->rootlist); slot->rootlist=NULL;
            } else if (slot->request.screen==APP_LYRICS) {
                (void)app_lyrics_deliver(h->app,slot->request,slot->lyrics_result,slot->lyrics); slot->lyrics=NULL;
            } else if (slot->request.screen>=APP_SEARCH) {
                (void)app_catalog_deliver(h->app,slot->request,slot->catalog_result,slot->catalog_page); slot->catalog_page=NULL;
            } else if (slot->request.screen==APP_PLAYLIST) {
                if (h->app->model.generation==slot->request.generation && h->app->model.request.serial==slot->request.serial) {
                    if (slot->source.read) { (void)playlist_source(&h->playlist,&slot->source,slot->request.generation); }
                    else {
                        h->app->model.pending=false; h->app->model.status=APP_ERROR; h->app->model.last_result=APP_FAILED;
                        h->playlist.pending=false; h->playlist.result=TRACK_IO;
                    }
                }
            } else { (void)app_deliver(h->app,slot->request,slot->result,slot->page); slot->page=NULL; }
            if (!slot->request.sidebar && slot->message[0] && h->app->model.generation==slot->request.generation) {
                strcpy_s(h->app->model.notice,sizeof(h->app->model.notice),slot->message);
            }
        }
        if (slot->page) { (void)page_release((JsonPage *)&slot->page->json); slot->page=NULL; }
        app_rootlist_release(slot->rootlist); slot->rootlist=NULL;
        if (slot->catalog_page) { (void)page_release((JsonPage *)&slot->catalog_page->json); slot->catalog_page=NULL; }
        if (slot->lyrics) { (void)lyrics_document_release(slot->lyrics); slot->lyrics=NULL; }
        atomic_store_explicit(&slot->phase,0,memory_order_release);
    }
    if (h->auth_waiting && enqueue(h,h->pending_auth,true,h->logout_waiting,false,false,(JsonString){0})==APP_PENDING) { h->auth_waiting=false; busy=true; }
    /* Only the worker accesses generation; use a retired acknowledgement slot,
     * not an unsynchronized read of the worker's generation. */
    uint64_t generation=busy ? 0 : atomic_exchange_explicit(&h->retirement,0,memory_order_acq_rel);
    if (generation && enqueue(h,(AppRequest){.generation=generation},false,false,false,true,(JsonString){0})!=APP_PENDING) {
        atomic_store_explicit(&h->retirement,generation,memory_order_release);
    }
}
static void cleanup_run(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; (void)cancel; AppLive *h=context;
    h->cleanup_success=live_account_close(h,false);
    reset(h,UINT64_MAX);
    h->cleanup_success=track_disk_close(&h->disk)&&h->cleanup_success;
}
static void cleanup_complete(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; AppLive *h=context;
    if (outcome!=WORKER_DONE) { h->cleanup_success=false; }
}
bool app_live_close(AppLive *h)
{
    if (!h) { return true; } h->stopping=true;
    if (h->worker) {
        track_filter_cancel(&h->filter);
        for (unsigned i=0;i<LIVE_SLOTS;++i) {
            if (atomic_load_explicit(&h->slots[i].phase,memory_order_acquire)==1) { (void)worker_cancel(h->worker,h->slots[i].ticket); }
        }
        if (WaitForSingleObject(worker_idle_handle(h->worker),120000)!=WAIT_OBJECT_0) { return false; }
        WorkerJob job={.run=cleanup_run,.complete=cleanup_complete,.context=h,.deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK};
        uint64_t ticket;
        if (worker_submit(h->worker,&job,&ticket)!=CORE_OK ||
            WaitForSingleObject(worker_idle_handle(h->worker),120000)!=WAIT_OBJECT_0 || !h->cleanup_success) { return false; }
        (void)worker_stop(h->worker);
        if (worker_join(h->worker,120000)!=CORE_OK) { return false; } h->worker=NULL;
    }
    track_filter_release(&h->filter); (void)track_filter_clear(&h->filter);
    if (!track_disk_close(&h->disk)) { return false; }
    if (GetFileAttributesW(h->index_path)!=INVALID_FILE_ATTRIBUTES) { (void)DeleteFileW(h->index_path); }
    for (unsigned i=0;i<LIVE_SLOTS;++i) {
        if (h->slots[i].page) { (void)page_release((JsonPage *)&h->slots[i].page->json); }
        app_rootlist_release(h->slots[i].rootlist);
        if (h->slots[i].catalog_page) { (void)page_release((JsonPage *)&h->slots[i].catalog_page->json); }
        if (h->slots[i].lyrics) { (void)lyrics_document_release(h->slots[i].lyrics); }
    }
    if (json_page_pool_dispose(h->pool)!=JSON_OK) { return false; }
    arena_dispose(&h->http_arena); arena_dispose(&h->secret_arena); arena_dispose(&h->token_arena);
    SecureZeroMemory(h,sizeof(*h)); return VirtualFree(h,0,MEM_RELEASE)!=0;
}
