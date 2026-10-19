#include "catalog_screens_test.h"
#include <stdio.h>

App cs_app;
AppFixture cs_host;
UiScheduler cs_scheduler;
PlayerSnapshot cs_player;
PlayerCommand cs_command;
AppRequest cs_latest;
unsigned cs_requests,cs_cancels,cs_commands;
uint64_t cs_now;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES],core_memory[MEMORY_BUDGET_CORE];
static Arena arena;
static EventQueue *events;
static bool asynchronous;
static DWORD handles;
static JsonString span(const char *s) { return (JsonString){s,strlen(s)}; }
static AppResult capture(void *context, const AppRequest *r, JsonString artwork)
{
    (void)context; (void)artwork; CHECK(!cs_app.widgets.in_frame); cs_latest=*r; ++cs_requests; return APP_PENDING;
}
static AppResult collection(void *context, const AppRequest *r) { return capture(context,r,(JsonString){0}); }
static void cancel(void *context, const AppRequest *r) { (void)context; CHECK(r->serial); ++cs_cancels; }
static void retire(void *context, uint64_t generation)
{
    (void)context;
    CHECK(catalog_reset(cs_host.catalog,generation)==CATALOG_OK);
    CHECK(lyrics_reset(cs_host.lyrics,generation)==LYRICS_OK);
}
static PlayerSnapshot snapshot(void *context) { (void)context; return cs_player; }
static PlayerResult submit(void *context, const PlayerCommand *c)
{
    (void)context; CHECK(!cs_app.widgets.in_frame); cs_command=*c; ++cs_commands;
    if (c->type==PLAYER_LOAD) {
        memcpy(cs_player.track_id,c->track_id,16); ++cs_player.generation;
        cs_player.state=PLAYER_PLAYING; cs_player.rate=48000; cs_player.timeline_valid=true;
        cs_player.position_frames=0; cs_player.duration_frames=240000;
    } else if (c->type==PLAYER_SEEK) { cs_player.position_frames=c->position_frames; ++cs_player.generation; }
    return PLAYER_OK;
}
void cs_open(bool async)
{
    (void)GetKeyState(VK_SHIFT); /* Warm this thread's Win32 input queue. */
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles)); asynchronous=async;
    cs_now=cs_requests=cs_cancels=cs_commands=0; cs_player=(PlayerSnapshot){.volume=0.75f};
    ui_scheduler_init(&cs_scheduler); ui_scheduler_visibility(&cs_scheduler,true,false,0);
    AppServices services={.request=collection,.catalog=capture,.cancel=cancel,.retire=retire,.submit=submit,.snapshot=snapshot,.demo=true};
    if (async) {
        CHECK(arena_init(&arena,core_memory,sizeof(core_memory)) && event_queue_create(&arena,16,&events)==CORE_OK);
        CHECK(app_fixture_init(&cs_host,&arena,events,&cs_app,L"" APP_FIXTURE_DIR)==APP_OK);
        services=app_fixture_services(&cs_host); services.submit=submit; services.snapshot=snapshot;
    } else {
        CHECK(json_page_pool_init(cs_host.pool_storage,sizeof(cs_host.pool_storage),&cs_host.pool)==JSON_OK);
    }
    CHECK(app_init(&cs_app,&services,&cs_scheduler,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    if (!async) {
        CatalogConfig config={.album_limit=20,.image_width=64}; LyricsConfig lyrics={0};
        CHECK(catalog_service_init(cs_app.catalog_storage,sizeof(cs_app.catalog_storage),cs_host.pool,&config,&cs_host.catalog)==CATALOG_OK);
        CHECK(lyrics_service_init(cs_app.lyrics_storage,sizeof(cs_app.lyrics_storage),cs_host.pool,&lyrics,&cs_host.lyrics)==LYRICS_OK);
    }
    CHECK(app_session(&cs_app,true,true,0)==APP_PENDING);
    cs_action(APP_GO_SEARCH,0); cs_settle();
}
void cs_tick(uint64_t now)
{
    cs_now=now;
    if (asynchronous) { app_fixture_pump(&cs_host); }
    (void)app_update(&cs_app,now);
}
void cs_settle(void)
{
    if (!asynchronous) { return; }
    for (unsigned i=0;i<200;++i) {
        cs_tick(cs_now);
        bool idle=atomic_load_explicit(&cs_host.retirement_phase,memory_order_acquire)==0;
        for (unsigned n=0;n<4;++n) { if (atomic_load_explicit(&cs_host.slots[n].phase,memory_order_acquire)) { idle=false; } }
        if (!cs_app.model.pending && idle) { return; }
        (void)WaitForSingleObject(event_wait_handle(events),50);
        Event batch[16]; size_t count; CHECK(event_drain(events,batch,16,&count)==CORE_OK);
    }
    CHECK(false);
}
void cs_action(AppActionType action, double value)
{
    CHECK(app_enqueue(&cs_app,(AppAction){.type=action,.value=value})==APP_OK);
    cs_tick(cs_now); cs_tick(cs_now); /* Catalog activation enqueues shared navigation. */
}
void cs_query(const char *text)
{
    CHECK(ui_text_set(&cs_app.widgets.texts[0],text,(uint32_t)strlen(text))==UI_WIDGET_OK);
    cs_action(APP_CATALOG_QUERY,0);
}
Clay_RenderCommandArray cs_layout(float width, float height, float dpi)
{
    Clay_RenderCommandArray c=app_layout(&cs_app,width,height,dpi,(UiWidgetInput){.x=-1,.y=-1});
    CHECK(c.length>0 && cs_app.widgets.error==UI_WIDGET_OK); return c;
}
void cs_response(const char *json, unsigned status)
{
    AppRequest *r=&cs_latest; CHECK(!asynchronous && cs_app.model.pending);
    if (r->screen==APP_LYRICS) {
        LyricsTrack track={.uri=span(r->uri),.artwork=span(cs_app.model.playing_artwork)}; LyricsRequest request;
        CHECK(lyrics_request(cs_host.lyrics,&track,r->generation,&request)==LYRICS_PENDING);
        size_t n=strlen(json); CHECK(n<=request.capacity); memcpy(request.body,json,n);
        const LyricsDocument *doc=NULL;
        LyricsResult result=lyrics_complete(cs_host.lyrics,request.ticket,LYRICS_OK,status,n,&doc);
        (void)app_lyrics_deliver(&cs_app,*r,result,doc);
    } else {
        CatalogRequest request; CatalogResult result;
        if (r->screen==APP_SEARCH) { result=search_request(cs_host.catalog,span(r->query),(SearchCategory)r->selector,r->cursor,20,r->generation,cs_now,&request); }
        else if (r->screen==APP_ALBUM) { result=album_request(cs_host.catalog,span(r->uri),r->cursor,r->generation,&request); }
        else { result=artist_request(cs_host.catalog,span(r->uri),(ArtistSection)r->selector,r->cursor,r->generation,&request); }
        CHECK(result==CATALOG_PENDING); size_t n=strlen(json); CHECK(n<=request.capacity); memcpy(request.body,json,n);
        const CatalogPage *page=NULL;
        result=catalog_complete(cs_host.catalog,request.ticket,CATALOG_OK,status,n,span(request.contract->persisted_sha256),&page);
        (void)app_catalog_deliver(&cs_app,*r,result,page);
    }
    cs_tick(cs_now);
}
void cs_fixture(const char *name)
{
    static char body[32*1024]; char path[512]; FILE *file=NULL;
    CHECK(snprintf(path,sizeof(path),"%s/%s.json",APP_FIXTURE_DIR,name)>0);
    CHECK(fopen_s(&file,path,"rb")==0 && file); size_t n=fread(body,1,sizeof(body)-1,file);
    CHECK(!ferror(file) && fgetc(file)==EOF && fclose(file)==0); body[n]=0; cs_response(body,200);
}
static void close_app(bool check_handles)
{
    app_dispose(&cs_app);
    if (asynchronous) {
        CHECK(app_fixture_stop(&cs_host)); CHECK(event_queue_close(events)==CORE_OK);
        Event batch[16]; size_t count; (void)event_drain(events,batch,16,&count);
        CHECK(event_queue_destroy(events)==CORE_OK); arena_dispose(&arena);
    }
    CHECK(json_page_pool_resident(cs_host.pool)==0);
    if (!asynchronous) { CHECK(json_page_pool_dispose(cs_host.pool)==JSON_OK); }
    DWORD after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&after));
    if (check_handles && after!=handles) { fprintf(stderr,"catalog handles: async=%u before=%lu after=%lu\n",asynchronous,handles,after); }
    if (check_handles) { CHECK(after==handles); }
}
void cs_close(void) { close_app(true); }
void cs_close_visual(void) { close_app(false); }
