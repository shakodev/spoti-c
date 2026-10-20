#include "catalog_screens_test.h"
#include "support/ui_heap_watch.h"
#include <stdio.h>

static void flow(void)
{
    cs_open(true);
    CHECK(cs_app.model.status==APP_EMPTY && json_page_pool_resident(cs_host.pool)==0);
    cs_query("m"); cs_tick(20); cs_query("mu"); cs_tick(40); cs_query("music");
    CHECK(cs_app.catalog.debounce==290 && !cs_app.model.pending);
    cs_tick(289); CHECK(!cs_app.model.pending && !cs_app.catalog.page);
    cs_tick(290); cs_settle(); CHECK(cs_app.catalog.page && cs_app.catalog.page->count==7);
    CHECK(cs_app.widgets.lists[0].focus==app_catalog_key(&cs_app,0));
    CHECK(json_page_pool_dispose(cs_host.pool)==JSON_INVALID); /* A live reader prevents reuse. */
    (void)cs_layout(1100,720,96);
    CHECK(cs_app.catalog.rendered<=7 && cs_app.control_count<APP_CONTROLS);
    const float widths[]={500,1375,1800}, heights[]={800,900,1125}, dpis[]={96,120,144};
    for (unsigned i=0;i<3;++i) {
        (void)cs_layout(widths[i],heights[i],dpis[i]);
        cs_app.widgets.focus=CLAY_ID("CatalogList").id;
        CHECK(app_key(&cs_app,APP_KEY_END,false)==APP_OK);
        CHECK(app_key(&cs_app,APP_KEY_HOME,false)==APP_OK);
        CHECK(app_key(&cs_app,APP_KEY_TAB,false)==APP_OK && app_key(&cs_app,APP_KEY_TAB,true)==APP_OK);
        Clay_ElementData bar=Clay_GetElementData(CLAY_ID("AppNowPlaying"));
        Clay_ElementData body=Clay_GetElementData(CLAY_ID("AppBody"));
        CHECK(bar.found && body.found && bar.boundingBox.y>=body.boundingBox.y+body.boundingBox.height);
    }
    (void)cs_layout(1100,720,96);
    cs_app.widgets.focus=CLAY_ID("CatalogList").id;
    CHECK(app_key(&cs_app,APP_KEY_HOME,false)==APP_OK);
    uint64_t first=app_catalog_key(&cs_app,0); CHECK(cs_app.widgets.lists[0].focus==first);
    CHECK(app_key(&cs_app,APP_KEY_ENTER,false)==APP_OK); cs_tick(cs_now); cs_tick(cs_now); cs_settle();
    CHECK(cs_app.model.location.screen==APP_ARTIST && cs_app.catalog.page->detail.id.bytes[15]==2);
    CHECK(cs_app.catalog.page->items[0].id.bytes[15]==1);
    cs_action(APP_CATALOG_SECTION,ARTIST_ALBUMS); cs_settle();
    CHECK(cs_app.catalog.page->count==1 && cs_app.catalog.page->items[0].kind==CATALOG_ALBUM);
    cs_action(APP_CATALOG_SELECT,0); cs_settle();
    CHECK(cs_app.model.location.screen==APP_ALBUM && cs_app.catalog.page->count==2);
    CHECK(strlen(cs_app.catalog.title)<=191 && cs_app.catalog.title[0]);
    CHECK(!strcmp(cs_app.catalog.artwork,"https://fixture.invalid/cover.png"));
    CHECK(strstr(cs_app.catalog.subtitle,"Synthetic artist"));
    CHECK(app_catalog_play_index(&cs_app)==0);
    const float hero_dpi[]={96,120,144,192};
    for (unsigned i=0;i<4;++i) {
        (void)cs_layout(1100*hero_dpi[i]/96,720*hero_dpi[i]/96,hero_dpi[i]);
        Clay_ElementData hero=Clay_GetElementData(CLAY_ID("CatalogHero"));
        Clay_ElementData list=Clay_GetElementData(CLAY_ID("CatalogList"));
        Clay_ElementData bar=Clay_GetElementData(CLAY_ID("AppNowPlaying"));
        CHECK(hero.found && list.found && bar.found);
        CHECK(hero.boundingBox.y+hero.boundingBox.height<=list.boundingBox.y);
        CHECK(list.boundingBox.y+list.boundingBox.height<=bar.boundingBox.y);
    }
    (void)cs_layout(1100,720,96);
    CHECK(Clay_GetElementData(CLAY_ID("CatalogHero")).found && Clay_GetElementData(CLAY_ID("CatalogPlay")).found);
    cs_action(APP_CATALOG_SELECT,1); CHECK(cs_app.model.last_result==APP_UNAVAILABLE && cs_commands==0);
    cs_app.widgets.focus=CLAY_ID("CatalogPlay").id;
    CHECK(app_key(&cs_app,APP_KEY_ENTER,false)==APP_OK); cs_tick(cs_now); cs_tick(cs_now);
    CHECK(cs_commands==1 && cs_command.type==PLAYER_LOAD && cs_command.track_id[15]==1);
    CHECK(cs_app.model.playing.id[15]==1 && cs_app.model.playing_artwork[0]);
    char artwork[512]; strcpy_s(artwork,sizeof(artwork),cs_app.catalog.artwork);
    cs_action(APP_MORE,0); CHECK(app_catalog_play_index(&cs_app)==CATALOG_END); cs_settle();
    CHECK(cs_app.catalog.page->offset==2 && cs_app.catalog.page->next==CATALOG_END);
    CHECK(cs_app.catalog.page->items[0].id.bytes[15]==5 && cs_app.catalog.title[0]);
    CHECK(!strcmp(artwork,cs_app.catalog.artwork));
    cs_action(APP_GO_LYRICS,0); cs_settle(); cs_tick(300);
    CHECK(cs_app.catalog.timeline.document && cs_app.catalog.lyric.line==0 && cs_app.catalog.lyric.valid);
    CHECK(cs_app.catalog.lyric.deadline_ms==800);
    cs_action(APP_SEEK,0.8); CHECK(cs_command.type==PLAYER_SEEK); cs_tick(310);
    CHECK(cs_app.catalog.lyric.line==8 && cs_app.widgets.lists[0].scroll==288);
    app_scroll(&cs_app,96); double manual=cs_app.widgets.lists[0].scroll;
    CHECK(cs_app.catalog.hold==5310); cs_action(APP_SEEK,0.1); cs_tick(320);
    CHECK(cs_app.catalog.lyric.line==1 && cs_app.widgets.lists[0].scroll==manual);
    cs_tick(5310); CHECK(cs_app.catalog.hold==CORE_CLOCK_NEVER && cs_app.widgets.lists[0].scroll==0);
    /* No wall-clock extrapolation: a stale audio snapshot cannot change line. */
    cs_tick(100000); CHECK(cs_app.catalog.lyric.line==1);
    ui_scheduler_visibility(&cs_scheduler,false,false,cs_now); cs_tick(cs_now);
    CHECK(cs_app.catalog.lyric.deadline_ms==CORE_CLOCK_NEVER && ui_scheduler_wait(&cs_scheduler,cs_now)==UINT32_MAX);
    ui_scheduler_visibility(&cs_scheduler,true,false,cs_now);
    cs_action(APP_BACK,0); cs_settle();
    CHECK(cs_app.model.location.screen==APP_ALBUM && !cs_app.catalog.timeline.document && !cs_scheduler.lyrics_visible);
    cs_action(APP_BACK,0); cs_settle(); CHECK(cs_app.model.location.selector==ARTIST_ALBUMS);
    cs_action(APP_BACK,0); CHECK(!strcmp(cs_app.model.location.query,"music"));
    cs_tick(cs_now+250); cs_settle(); CHECK(cs_app.model.location.screen==APP_SEARCH);
    cs_action(APP_MORE,0); cs_settle(); CHECK(cs_app.catalog.page->offset==20 && cs_app.catalog.page->count==1);
    cs_action(APP_GO_SETTINGS,0); cs_settle(); CHECK(json_page_pool_resident(cs_host.pool)==0);
    cs_close();
}
static void scheduling(void)
{
    cs_open(true); cs_query("music"); cs_tick(250); cs_settle();
    cs_action(APP_CATALOG_SELECT,1); cs_settle(); cs_action(APP_CATALOG_SELECT,0);
    cs_action(APP_GO_LYRICS,0); cs_settle();
    (void)ui_scheduler_frame(&cs_scheduler,250);
    cs_scheduler.stats=(UiSchedulerStats){0};
    for (uint64_t now=251;now<=2250;++now) {
        uint32_t reasons=ui_scheduler_frame(&cs_scheduler,now);
        if (reasons) { cs_player.position_frames=(now-250)*48; cs_tick(now); (void)ui_scheduler_frame(&cs_scheduler,now); }
    }
    CHECK(cs_scheduler.stats.progress_ticks==2 && cs_scheduler.stats.lyrics_ticks==4);
    CHECK(cs_scheduler.stats.animation_ticks==0 && cs_app.catalog.lyric.line==4);
    cs_player.state=PLAYER_PAUSED; cs_tick(2251); (void)ui_scheduler_frame(&cs_scheduler,2251);
    CHECK(ui_scheduler_wait(&cs_scheduler,2251)==UINT32_MAX);
    cs_close();
}
static void play_admission(void)
{
    cs_open(false); cs_query("music"); cs_tick(250); cs_fixture("search");
    CHECK(app_catalog_play_index(&cs_app)==2); /* Artist and album results are navigation, not playback. */
    bool online=cs_app.model.online; cs_app.model.online=false;
    CHECK(app_catalog_play_index(&cs_app)==CATALOG_END); cs_app.model.online=online;
    cs_app.model.logged_in=false; CHECK(app_catalog_play_index(&cs_app)==CATALOG_END); cs_app.model.logged_in=true;
    cs_action(APP_CATALOG_SELECT,1); cs_fixture("album");
    cs_action(APP_MORE,0);
    /* Real continuation contracts may contain only tracks, without a repeated hero. */
    cs_response("{\"data\":{\"albumUnion\":{\"__typename\":\"Album\",\"uri\":\"spotify:album:0000000000000000000003\","
        "\"tracksV2\":{\"items\":[],\"totalCount\":2,\"pagingInfo\":{\"nextOffset\":null}}}}}",200);
    CHECK(cs_app.catalog.title[0] && !strcmp(cs_app.catalog.artwork,"https://fixture.invalid/cover.png"));
    CHECK(app_catalog_play_index(&cs_app)==CATALOG_END);
    cs_action(APP_GO_SETTINGS,0); cs_close();
}
static void soak(void)
{
    cs_open(true); cs_query("music"); cs_tick(250); cs_settle();
    for (unsigned i=0;i<300;++i) {
        cs_action(APP_CATALOG_SELECT,0); cs_settle(); CHECK(cs_app.model.location.screen==APP_ARTIST);
        cs_action(APP_BACK,0); cs_tick(cs_now+250); cs_settle();
        CHECK(cs_app.model.location.screen==APP_SEARCH && cs_app.model.history_count==1);
        CHECK(json_page_pool_resident(cs_host.pool)==1);
        (void)cs_layout(1100,720,96);
    }
    ui_heap_begin();
    for (unsigned i=0;i<1000;++i) { (void)cs_layout(1100,720,96); }
    ui_heap_end();
    cs_action(APP_GO_SETTINGS,0); cs_settle(); CHECK(json_page_pool_resident(cs_host.pool)==0);
    printf("catalog: App=%zu host-model=%zu; 600 route transitions, 1000 allocation-free layouts\n",sizeof(App),sizeof(AppFixture)-MEMORY_BUDGET_API_PAGES);
    cs_close();
}
static DWORD WINAPI run(void *context)
{
    bool visual=*(bool *)context;
    flow(); scheduling(); play_admission(); soak(); cs_faults();
    if (visual) { return (DWORD)cs_visual(); }
    puts("app_catalog_screens: fixture routes, stable IDs, debounce, pages, lyrics, keyboard and ownership passed"); return 0;
}
int main(int argc, char **argv)
{
    bool visual=argc==2 && !strcmp(argv[1],"--visual");
    HANDLE thread=CreateThread(NULL,0,run,&visual,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread,115000)==WAIT_OBJECT_0);
    DWORD result; CHECK(GetExitCodeThread(thread,&result) && CloseHandle(thread)); return (int)result;
}
