#include "app_fixture.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static JsonString span(const char *s) { return (JsonString){s,strlen(s)}; }
void app_fixture_reset(AppFixture *h, uint64_t generation)
{
    if (generation<=h->generation) { return; }
    (void)collection_reset(h->service,generation);
    if (h->catalog) { (void)catalog_reset(h->catalog,generation); }
    if (h->lyrics) { (void)lyrics_reset(h->lyrics,generation); }
    h->generation=generation;
}
static bool initialize(AppFixture *h)
{
    if (!h->catalog) {
        CatalogConfig config={.locale={"en",2},.album_limit=20,.image_width=64};
        if (catalog_service_init(h->app->catalog_storage,sizeof(h->app->catalog_storage),h->pool,&config,&h->catalog)!=CATALOG_OK) { return false; }
        (void)catalog_reset(h->catalog,h->generation);
    }
    if (!h->lyrics) {
        LyricsConfig config={0};
        if (lyrics_service_init(h->app->lyrics_storage,sizeof(h->app->lyrics_storage),h->pool,&config,&h->lyrics)!=LYRICS_OK) { return false; }
        (void)lyrics_reset(h->lyrics,h->generation);
    }
    return true;
}
static bool body(AppFixture *h, const wchar_t *name, char *buffer, size_t capacity, size_t *bytes)
{
    wchar_t path[320]; FILE *file=NULL;
    if (swprintf_s(path,320,L"%s/%s.json",h->directory,name)<=0 || _wfopen_s(&file,path,L"rb") || !file) { return false; }
    *bytes=fread(buffer,1,capacity,file); bool ok=!ferror(file) && fgetc(file)==EOF;
    return fclose(file)==0 && ok;
}
void app_fixture_catalog_run(AppFixture *h, AppFixtureSlot *slot, const CancelToken *cancel)
{
    if (!initialize(h)) { slot->catalog_result=CATALOG_NO_MEMORY; slot->lyrics_result=LYRICS_NO_MEMORY; return; }
    AppRequest *r=&slot->request;
    if (r->screen==APP_LYRICS) {
        LyricsRequest request;
        LyricsTrack track={.uri=span(r->uri),.artwork=span(slot->artwork),.client_language={"en",2}};
        slot->lyrics_result=lyrics_request(h->lyrics,&track,r->generation,&request);
        if (slot->lyrics_result!=LYRICS_PENDING || !request.ticket.serial) { return; }
        size_t bytes=0; bool ok=body(h,L"lyrics",request.body,request.capacity,&bytes);
        LyricsResult result=worker_cancelled(cancel) ? LYRICS_CANCELLED : ok ? LYRICS_OK : LYRICS_ERROR;
        slot->lyrics_result=lyrics_complete(h->lyrics,request.ticket,result,200,bytes,&slot->lyrics); return;
    }
    CatalogRequest request;
    if (r->screen==APP_SEARCH) {
        slot->catalog_result=search_request(h->catalog,span(r->query),(SearchCategory)r->selector,r->cursor,20,r->generation,0,&request);
    } else if (r->screen==APP_ALBUM) { slot->catalog_result=album_request(h->catalog,span(r->uri),r->cursor,r->generation,&request); }
    else { slot->catalog_result=artist_request(h->catalog,span(r->uri),(ArtistSection)r->selector,r->cursor,r->generation,&request); }
    if (slot->catalog_result!=CATALOG_PENDING || !request.ticket.serial) { return; }
    const wchar_t *name=r->screen==APP_SEARCH ? (r->cursor ? L"search-next" : L"search") :
        r->screen==APP_ALBUM ? (r->cursor ? L"album-next" : L"album") : L"artist";
    size_t bytes=0; bool ok=body(h,name,request.body,request.capacity,&bytes);
    CatalogResult result=worker_cancelled(cancel) ? CATALOG_CANCELLED : ok ? CATALOG_OK : CATALOG_ERROR;
    slot->catalog_result=catalog_complete(h->catalog,request.ticket,result,200,bytes,span(request.contract->persisted_sha256),&slot->catalog_page);
}
static void retire_run(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; (void)cancel; AppFixture *h=context;
    h->retired=atomic_load_explicit(&h->retire_generation,memory_order_acquire);
    app_fixture_reset(h,h->retired);
}
static void retire_complete(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; (void)outcome; AppFixture *h=context;
    atomic_store_explicit(&h->retirement_phase,2,memory_order_release);
    Event wake={.type=APP_FIXTURE_EVENT}; (void)event_post(h->events,&wake);
}
void app_fixture_retirement_pump(AppFixture *h)
{
    unsigned phase=atomic_load_explicit(&h->retirement_phase,memory_order_acquire);
    if (phase==1 || h->stopping) { return; }
    if (phase==2) { atomic_store(&h->retirement_phase,0); }
    if (atomic_load_explicit(&h->retire_generation,memory_order_acquire)<=h->retired) { return; }
    atomic_store(&h->retirement_phase,1);
    WorkerJob job={.run=retire_run,.complete=retire_complete,.context=h,.deadline=CORE_CLOCK_NEVER,.priority=WORKER_DISK};
    uint64_t ticket;
    if (worker_submit(h->worker,&job,&ticket)!=CORE_OK) { atomic_store(&h->retirement_phase,0); }
}
void app_fixture_retire(void *context, uint64_t generation)
{
    AppFixture *h=context;
    atomic_store_explicit(&h->retire_generation,generation,memory_order_release);
    app_fixture_retirement_pump(h);
}
