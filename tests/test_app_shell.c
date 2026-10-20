#include "app_shell_support.h"
#include "player_test.h"
#include "support/ui_heap_watch.h"
#include <math.h>
static bool persistent_only;

static void states(void)
{
    shell_open(pt.player);
    CHECK(app.model.status==APP_SIGNED_OUT);
    CHECK(app_session(&app,true,true,0)==APP_PENDING && app.model.status==APP_LOADING);
    shell_deliver("home"); CHECK(app.model.status==APP_CONTENT && app.model.page->count==3);
    shell_action(APP_GO_LIBRARY); CHECK(app.model.pending && app.model.page==NULL && app.model.history_count==1);
    shell_deliver("library"); CHECK(app.model.page->count==2 && app.model.page->total==4);
    CHECK(app.model.page->items[1].name.bytes>5);
    shell_action(APP_MORE); CHECK(latest.cursor==2 && app.model.page);
    shell_deliver("library-next"); CHECK(app.model.page->offset==2 && app.model.page->next==COLLECTION_END);
    shell_action(APP_BACK); CHECK(latest.kind==COLLECTION_HOME); shell_deliver("home");
    shell_action(APP_GO_SETTINGS); CHECK(app.model.page==NULL && !app.model.pending);
    shell_action(APP_BACK); shell_deliver("home");
    shell_action(APP_RETRY); AppRequest old=latest;
    const CollectionPage *stale=shell_page(old,"home");
    shell_action(APP_GO_LIBRARY); CHECK(app_deliver(&app,old,COLLECTION_OK,stale)==APP_STALE);
    CHECK(app.model.page==NULL && app.stale_deliveries==1); shell_deliver("library");
    shell_action(APP_RETRY); CHECK(app_deliver(&app,latest,COLLECTION_AUTH,NULL)==APP_FAILED);
    CHECK(app.model.status==APP_AUTH_EXPIRED && !app.model.page);
    shell_action(APP_SIGN_IN); CHECK(authentications==1 && app.model.auth_pending);
    CHECK(app_session(&app,true,true,10)==APP_PENDING); shell_deliver("library");
    shell_action(APP_RETRY); CHECK(app_deliver(&app,latest,COLLECTION_ERROR,NULL)==APP_FAILED);
    CHECK(app.model.status==APP_ERROR);
    CHECK(app_session(&app,true,false,20)==APP_UNAVAILABLE && app.model.status==APP_OFFLINE);
    CHECK(app_session(&app,true,true,30)==APP_PENDING); shell_deliver("library");
    shell_action(APP_RETRY); shell_deliver("empty"); CHECK(app.model.status==APP_EMPTY);
    Clay_RenderCommandArray empty=shell_layout(1100,720,96); bool empty_label=false;
    for (int32_t i=0;i<empty.length;++i) {
        Clay_RenderCommand *c=&empty.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT && c->renderData.text.stringContents.length==17 &&
            !memcmp(c->renderData.text.stringContents.chars,"Nothing here yet.",17)) { empty_label=true; }
    }
    CHECK(empty_label); shell_action(APP_RETRY); shell_deliver("library");
    AppEntity private_entity={.kind=COLLECTION_ALBUM,.title="Private title",.uri="private:entity"};
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SELECT,.entity=private_entity})==APP_OK);
    (void)app_update(&app,50); CHECK(app.model.selected.title[0]);
    shell_action(APP_SIGN_OUT); CHECK(!app.model.logged_in && !app.model.page && app.model.history_count==0);
    CHECK(!app.model.playing.title[0] && !app.model.selected.title[0] && authentications==2);
    CHECK(cancellations>0); shell_close(); pt_drive();
}
static void bounds_and_keyboard(void)
{
    shell_open(pt.player); CHECK(app_session(&app,true,true,0)==APP_PENDING);
    shell_action(APP_GO_LIBRARY); shell_deliver("library");
    for (unsigned i=0;i<100;++i) {
        AppGeometry g; float dpi=96+(float)(i%5)*24, width=400+(float)(i%7)*180;
        CHECK(app_geometry(width*dpi/96,640*dpi/96,dpi,true,&g)==APP_OK);
        CHECK(fabsf(g.width-width)<0.01f && g.main>=280 && g.sidebar==(width<760?64:280));
        (void)shell_layout(width*dpi/96,640*dpi/96,dpi);
        Clay_ElementData bar=Clay_GetElementData(CLAY_ID("AppNowPlaying"));
        Clay_ElementData body=Clay_GetElementData(CLAY_ID("AppBody"));
        CHECK(bar.found && body.found && bar.boundingBox.y>=body.boundingBox.y+body.boundingBox.height);
        CHECK(bar.boundingBox.height==(width<760 ? 120 : 88));
    }
    CHECK(app_geometry(NAN,640,96,false,&app.geometry)==APP_INVALID);
    CHECK(app_geometry(399,640,96,false,&app.geometry)==APP_FULL);
    CHECK(app_geometry(800,240,96,false,&app.geometry)==APP_FULL);
    (void)shell_layout(1100,720,96);
    app.widgets.focus=CLAY_ID("AppList").id;
    CHECK(app_key(&app,APP_KEY_END,false)==APP_OK);
    CHECK(app.widgets.lists[0].focus==2);
    CHECK(app_key(&app,APP_KEY_HOME,false)==APP_OK && app.widgets.lists[0].focus==1);
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,0);
    CHECK(!strcmp(app.model.selected.title,"Evening rotation"));
    CHECK(app.model.location.screen==APP_PLAYLIST && app.model.last_result==APP_UNAVAILABLE);
    shell_action(APP_BACK); shell_deliver("library"); (void)shell_layout(1100,720,96);
    app.widgets.focus=CLAY_ID("NavSettings").id;
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,0);
    CHECK(app.model.location.screen==APP_SETTINGS);
    (void)shell_layout(1100,720,96);
    CHECK(app_key(&app,APP_KEY_TAB,false)==APP_OK);
    CHECK(app_key(&app,APP_KEY_TAB,true)==APP_OK);
    CHECK(app_key(&app,APP_KEY_BACK,false)==APP_OK); (void)app_update(&app,0); shell_deliver("library");
    for (unsigned i=0;i<APP_HISTORY;++i) { shell_action(APP_GO_SETTINGS); }
    uint64_t gen=app.model.generation; shell_action(APP_GO_HOME);
    CHECK(app.model.last_result==APP_FULL && app.model.generation==gen && app.model.history_count==APP_HISTORY);
    for (unsigned i=0;i<APP_ACTIONS;++i) { CHECK(app_enqueue(&app,(AppAction){.type=APP_DETAIL})==APP_OK); }
    CHECK(app_enqueue(&app,(AppAction){.type=APP_DETAIL})==APP_FULL); (void)app_update(&app,0);
    for (unsigned i=0;i<APP_INPUTS;++i) { CHECK(app_pointer(&app,(UiWidgetInput){.pressed=true})==APP_OK); }
    CHECK(app_pointer(&app,(UiWidgetInput){.released=true})==APP_FULL);
    CHECK(app.input_count==1 && app.inputs[0].cancel);
    shell_close();
}
static void player_and_frames(void)
{
    shell_open(pt.player); CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    AppEntity track={.kind=COLLECTION_TRACK,.title="Fixture track",.uri="fixture:track",.id={1},.playable=true};
    memset(track.id,0,16); track.id[15]=1;
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SELECT,.entity=track})==APP_OK);
    (void)app_update(&app,0); pt_drive(); (void)app_update(&app,0);
    CHECK(app.model.player.state==PLAYER_PLAYING && app.scheduler->playing);
    pt_render(480); pt_drive(); (void)app_update(&app,1000);
    PlayerSnapshot snapshot=player_snapshot(pt.player);
    CHECK(app.model.player.position_frames==snapshot.position_frames && app.model.player.timeline_valid);
    uint64_t position=app.model.player.position_frames;
    (void)shell_layout(1100,720,96); (void)shell_layout(1100,720,96);
    CHECK(app.model.player.position_frames==position); /* No wall-clock interpolation. */
    shell_action(APP_TOGGLE_PLAY); pt_drive(); (void)app_update(&app,1000);
    CHECK(app.model.player.state==PLAYER_PAUSED && !app.scheduler->playing);
    (void)ui_scheduler_frame(app.scheduler,1000); CHECK(ui_scheduler_deadline(app.scheduler)==CORE_CLOCK_NEVER);
    shell_action(APP_GO_LIBRARY); shell_deliver("library"); CHECK(!strcmp(app.model.playing.title,"Fixture track"));
    (void)shell_layout(1100,720,96); app.widgets.focus=CLAY_ID("AppSeek").id;
    CHECK(app_key(&app,APP_KEY_HOME,false)==APP_OK); (void)app_update(&app,1000); pt_drive();
    (void)app_update(&app,1000); CHECK(app.model.player.position_frames<=480);
    ui_heap_begin();
    for (unsigned i=0;i<2000;++i) { (void)shell_layout(1100,720,96); }
    ui_heap_end();
    for (unsigned i=0;i<300;++i) {
        shell_action(APP_GO_HOME); shell_deliver("home"); (void)shell_layout(1100,720,96);
        shell_action(APP_BACK); shell_deliver("library");
        CHECK(collection_resident_pages(collection)==1 && json_page_pool_resident(pages)==1);
    }
    printf("app state=%zu bytes; 2000 warm layouts: zero Debug heap calls; 600 route transitions bounded\n",sizeof(App));
    shell_close();
}
static AppFixture async_host;
static _Alignas(16) unsigned char host_core[MEMORY_BUDGET_CORE];
static void asynchronous(void)
{
    Arena core={0}; EventQueue *events=NULL;
    CHECK(arena_init(&core,host_core,sizeof(host_core)) && event_queue_create(&core,16,&events)==CORE_OK);
    shell_open(pt.player); app_dispose(&app);
    CHECK(app_fixture_init(&async_host,&core,events,&app,L"" APP_FIXTURE_DIR)==APP_OK);
    AppServices services=app_fixture_services(&async_host);
    static _Alignas(64) unsigned char clay[UI_CLAY_BYTES]; static unsigned char scratch[UI_SCRATCH_BYTES];
    CHECK(app_init(&app,&services,NULL,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    CHECK(app_session(&app,true,true,0)==APP_PENDING);
    for (unsigned i=0;i<20 && !app.model.page;++i) {
        (void)WaitForSingleObject(event_wait_handle(events),100);
        Event batch[16]; size_t n; CHECK(event_drain(events,batch,16,&n)==CORE_OK); app_fixture_pump(&async_host);
    }
    CHECK(app.model.page && app.model.page->kind==COLLECTION_HOME);
    AppEntity entity={.kind=COLLECTION_PLAYLIST_ITEM,.title="Offline playlist",.uri="fixture:playlist"};
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SELECT,.entity=entity})==APP_OK); (void)app_update(&app,0);
    for (unsigned i=0;i<50 && !async_host.playlist.ready;++i) {
        (void)WaitForSingleObject(event_wait_handle(events),100);
        Event batch[16]; size_t n; CHECK(event_drain(events,batch,16,&n)==CORE_OK);
        app_fixture_pump(&async_host); (void)app_update(&app,0);
    }
    CHECK(async_host.playlist.ready && async_host.playlist.count==10000 && async_host.playlist.source.total==10005);
    CHECK(async_host.playlist.coverage==TRACK_WINDOW && async_host.disk_path[0]);
    shell_action(APP_GO_LIBRARY); shell_action(APP_GO_HOME); /* Cancel worker or queued completion. */
    app_dispose(&app); CHECK(app_fixture_stop(&async_host));
    CHECK(event_queue_close(events)==CORE_OK); Event batch[16]; size_t n;
    (void)event_drain(events,batch,16,&n); CHECK(event_queue_destroy(events)==CORE_OK); arena_dispose(&core);
}
static DWORD WINAPI run(void *context)
{
    bool visual=*(bool *)context;
    if (persistent_only) { shell_sidebar(); return 0; }
    pt_load_fixtures(); pt_open(); states(); bounds_and_keyboard(); player_and_frames(); asynchronous();
    shell_account(); shell_artwork(); shell_volume(); shell_browse(); shell_visibility(); shell_now_playing(); shell_sidebar();
    pt_close();
    shell_font_frames(); shell_native();
    int result=0;
    if (visual) {
        shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
        result=shell_visual(); shell_close();
    }
    puts("app_shell: routes, immutable pages, stale delivery, auth/offline, keyboard, playback and bounds passed");
    return (DWORD)result;
}
int main(int argc, char **argv)
{
    bool visual=argc==2 && !strcmp(argv[1],"--visual");
    persistent_only=argc==2 && !strcmp(argv[1],"--persistent");
    HANDLE thread=CreateThread(NULL,0,run,&visual,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread,85000)==WAIT_OBJECT_0);
    DWORD result; CHECK(GetExitCodeThread(thread,&result) && CloseHandle(thread)); return (int)result;
}
