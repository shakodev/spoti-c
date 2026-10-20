#include "app_fixture.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void run(const Event *event, const CancelToken *cancel, void *context)
{
    AppFixture *h = context; AppFixtureSlot *slot = &h->slots[event->id];
    slot->result = COLLECTION_CANCELLED;
    if (worker_cancelled(cancel)) { return; }
    app_fixture_reset(h,slot->request.generation);
    if (slot->authentication) {
        slot->result = COLLECTION_OK; return;
    }
    if (slot->request.sidebar) { app_fixture_sidebar_run(h,slot,cancel); return; }
    if (slot->request.screen>=APP_SEARCH) { app_fixture_catalog_run(h,slot,cancel); return; }
    if (slot->request.kind==COLLECTION_PLAYLIST) {
        slot->playlist=true; slot->result=app_fixture_playlist_open(h,slot,cancel); return;
    }
    CollectionRequest request;
    slot->result = collection_fetch(h->service,slot->request.kind,slot->request.cursor,
        slot->request.generation,&request);
    if (slot->result != COLLECTION_PENDING || !request.ticket.serial) { return; }
    h->generation = slot->request.generation;
    const wchar_t *name = slot->request.kind == COLLECTION_HOME ? L"home.json" :
        slot->request.kind == COLLECTION_HOME_SECTION ? L"shelf.json" : slot->request.cursor ? L"library-next.json" : L"library.json";
    wchar_t path[300];
    int written = swprintf_s(path,300,L"%s/%s",h->directory,name);
    FILE *file = NULL; size_t bytes = 0;
    bool ok = written > 0 && _wfopen_s(&file,path,L"rb") == 0 && file;
    if (ok) { bytes = fread(request.body,1,request.capacity,file); ok = !ferror(file) && fgetc(file) == EOF; ok = fclose(file) == 0 && ok; }
    CollectionResult result = worker_cancelled(cancel) ? COLLECTION_CANCELLED : ok ? COLLECTION_OK : COLLECTION_ERROR;
    slot->result = collection_complete(h->service,request.ticket,result,200,bytes,&slot->page);
}
static void complete(const Event *event, WorkerOutcome outcome, void *context)
{
    AppFixture *h = context; AppFixtureSlot *slot = &h->slots[event->id];
    if (outcome != WORKER_DONE && !slot->page) { slot->result = COLLECTION_CANCELLED; }
    Event notification = {.type=APP_FIXTURE_EVENT,.id=event->id};
    atomic_store_explicit(&slot->phase,2,memory_order_release);
    /* FULL retains a published slot; the already-signaled queue wakes UI prep,
     * which scans all four slots rather than relying on one event per page. */
    (void)event_post(h->events,&notification);
}
static AppResult submit(AppFixture *h, const AppRequest *request, bool auth, AppAuth action, JsonString artwork)
{
    if (!h || h->stopping) { return APP_UNAVAILABLE; }
    for (unsigned i = 0; i < 4; ++i) {
        AppFixtureSlot *slot = &h->slots[i];
        if (atomic_load_explicit(&slot->phase,memory_order_acquire) != 0) { continue; }
        slot->request = *request; slot->authentication = auth; slot->auth = action;
        slot->playlist=false;
        slot->artwork[0]=0;
        if (artwork.bytes && json_base_string_copy(artwork,slot->artwork,sizeof(slot->artwork))!=JSON_OK) { return APP_FULL; }
        slot->catalog_page=NULL; slot->lyrics=NULL; slot->catalog_result=CATALOG_CANCELLED; slot->lyrics_result=LYRICS_CANCELLED;
        slot->page = NULL; slot->rootlist=NULL; slot->result = COLLECTION_CANCELLED;
        atomic_store(&slot->phase,1);
        WorkerJob job = {.request={.id=i},.run=run,.complete=complete,.context=h,.deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK};
        if (worker_submit(h->worker,&job,&slot->ticket) != CORE_OK) { atomic_store(&slot->phase,0); return APP_FULL; }
        return APP_PENDING;
    }
    return APP_FULL;
}
static AppResult request(void *context, const AppRequest *request)
{
    return submit(context,request,false,APP_LOGIN,(JsonString){0});
}
static AppResult catalog(void *context, const AppRequest *r, JsonString artwork)
{
    AppFixture *h=context;
    if (artwork.bytes>=sizeof(h->slots[0].artwork)) { return APP_FULL; }
    return submit(h,r,false,APP_LOGIN,artwork);
}
static void cancel(void *context, const AppRequest *request)
{
    AppFixture *h = context;
    for (unsigned i = 0; i < 4; ++i) {
        if (atomic_load_explicit(&h->slots[i].phase,memory_order_acquire) == 1 &&
            !h->slots[i].authentication && h->slots[i].request.serial == request->serial) {
            (void)worker_cancel(h->worker,h->slots[i].ticket);
        }
    }
}
static AppResult auth(void *context, AppAuth action)
{
    AppFixture *h = context;
    if (action==APP_LOGOUT) {
        for (unsigned i=0;i<4;++i) {
            if (atomic_load_explicit(&h->slots[i].phase,memory_order_acquire)==1 && h->slots[i].authentication) {
                (void)worker_cancel(h->worker,h->slots[i].ticket);
            }
        }
    }
    AppRequest r = {.generation=h->app->model.generation,.serial=h->app->model.auth_serial};
    return submit(h,&r,true,action,(JsonString){0});
}
AppServices app_fixture_services(AppFixture *h)
{
    return (AppServices){.request=request,.sidebar=request,.catalog=catalog,.cancel=cancel,.retire=app_fixture_retire,.auth=auth,.context=h,.demo=true,.playlist=&h->playlist};
}
AppResult app_fixture_init(AppFixture *h, Arena *core, EventQueue *events, App *app, const wchar_t *directory)
{
    if (!h || !core || !events || !app || !directory || wcslen(directory) >= 256) { return APP_INVALID; }
    memset(h,0,sizeof(*h)); h->app = app; h->events = events;
    h->disk.file=INVALID_HANDLE_VALUE;
    wcscpy_s(h->directory,256,directory);
    if (json_page_pool_init(h->pool_storage,sizeof(h->pool_storage),&h->pool) != JSON_OK ||
        collection_service_init(h->service_storage,sizeof(h->service_storage),h->pool,&h->service) != COLLECTION_OK) { return APP_FAILED; }
    h->rootlists=app_rootlist_store(h->service_storage,sizeof(h->service_storage));
    if (!h->rootlists) { return APP_FULL; }
    for (unsigned i = 0; i < 4; ++i) { atomic_init(&h->slots[i].phase,0); }
    atomic_init(&h->retirement_phase,0); atomic_init(&h->retire_generation,0);
    if (worker_start(core,4,(CoreClock){0},&h->worker)!=CORE_OK || track_filter_init(&h->filter,h->worker,events)!=TRACK_OK) { return APP_FAILED; }
    playlist_init(&h->playlist,&h->filter); return APP_OK;
}
void app_fixture_pump(AppFixture *h)
{
    if (!h || !h->worker) { return; }
    for (unsigned i = 0; i < 4; ++i) {
        AppFixtureSlot *slot = &h->slots[i];
        if (atomic_load_explicit(&slot->phase,memory_order_acquire) != 2) { continue; }
        if (slot->authentication) {
            if (slot->request.serial == h->app->model.auth_serial) {
                if (slot->result==COLLECTION_OK) { (void)app_session(h->app,slot->auth == APP_LOGIN,true,GetTickCount64()); }
                else { h->app->model.auth_pending=false; h->app->model.last_result=APP_FAILED; }
            }
        } else if (slot->request.sidebar) {
            (void)app_sidebar_deliver(h->app,slot->request,slot->result,slot->rootlist); slot->rootlist=NULL;
        } else if (slot->request.screen==APP_LYRICS) {
            (void)app_lyrics_deliver(h->app,slot->request,slot->lyrics_result,slot->lyrics); slot->lyrics=NULL;
        } else if (slot->request.screen>=APP_SEARCH) {
            (void)app_catalog_deliver(h->app,slot->request,slot->catalog_result,slot->catalog_page); slot->catalog_page=NULL;
        } else if (slot->playlist && h->app->alive && h->app->model.generation==slot->request.generation &&
            h->app->model.request.serial==slot->request.serial) {
            if (slot->result==COLLECTION_OK) { (void)playlist_source(&h->playlist,&slot->source,slot->request.generation); }
            else { h->app->model.pending=false; h->playlist.pending=false; h->playlist.result=TRACK_IO; }
        } else { (void)app_deliver(h->app,slot->request,slot->result,slot->page); }
        slot->page = NULL; atomic_store_explicit(&slot->phase,0,memory_order_release);
    }
    app_fixture_retirement_pump(h);
}
bool app_fixture_stop(AppFixture *h)
{
    if (!h) { return true; }
    if (!h->worker) { return app_fixture_playlist_close(h); }
    h->stopping = true; (void)worker_stop(h->worker);
    if (worker_join(h->worker,5000) != CORE_OK) { return false; }
    h->worker = NULL;
    if (!app_fixture_playlist_close(h)) { return false; }
    track_filter_release(&h->filter); (void)track_filter_clear(&h->filter); memset(&h->last_queue,0,sizeof(h->last_queue));
    for (unsigned i = 0; i < 4; ++i) {
        if (h->slots[i].page) { (void)page_release((JsonPage *)&h->slots[i].page->json); h->slots[i].page = NULL; }
        app_rootlist_release(h->slots[i].rootlist); h->slots[i].rootlist=NULL;
        if (h->slots[i].catalog_page) { (void)page_release((JsonPage *)&h->slots[i].catalog_page->json); h->slots[i].catalog_page=NULL; }
        if (h->slots[i].lyrics) { (void)lyrics_document_release(h->slots[i].lyrics); h->slots[i].lyrics=NULL; }
    }
    if (h->generation < UINT64_MAX) { app_fixture_reset(h,h->generation+1); }
    return collection_resident_pages(h->service) == 0 && json_page_pool_dispose(h->pool)==JSON_OK;
}
