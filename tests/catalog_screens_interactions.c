#include "catalog_screens_test.h"
#include <stdio.h>

static void focus_item(uint32_t index)
{
    for (uint32_t n=0;n<cs_app.control_count;++n) {
        const AppControl *c=&cs_app.controls[n];
        if (c->action.type==APP_CATALOG_SELECT && c->action.value==index) { cs_app.widgets.focus=c->id.id; return; }
    }
    CHECK(false);
}
static void menus(void)
{
    cs_open(false); cs_query("music"); cs_tick(250); cs_fixture("search");
    (void)cs_layout(1100,720,96); focus_item(2);
    uint32_t focus=cs_app.widgets.focus;
    CHECK(app_context_open(&cs_app,0,0,true)==APP_OK && cs_app.menu.count==2);
    CHECK(!strcmp(cs_app.menu.labels[0],"Play") && !strcmp(cs_app.menu.labels[1],"Go to album"));
    (void)cs_layout(1100,720,96);
    CHECK(Clay_GetElementData(CLAY_ID("RouteMenu")).found);
    CHECK(app_key(&cs_app,APP_KEY_ESCAPE,false)==APP_OK && !cs_app.menu.open && cs_app.widgets.focus==focus);
    CHECK(!cs_commands && app_context_open(&cs_app,0,0,true)==APP_OK);
    CHECK(app_key(&cs_app,APP_KEY_DOWN,false)==APP_OK && cs_app.menu.selected==1);
    CHECK(app_key(&cs_app,APP_KEY_ENTER,false)==APP_OK && !cs_app.menu.open);
    cs_tick(250); CHECK(cs_app.model.location.screen==APP_ALBUM && !cs_commands); cs_fixture("album");
    (void)cs_layout(1100,720,96); focus_item(1); /* restricted track */
    CHECK(app_context_open(&cs_app,0,0,true)==APP_OK && cs_app.menu.disabled[0] && cs_app.menu.reason[0]);
    CHECK(cs_app.menu.selected==1); /* enabled album navigation skips unavailable Play */
    cs_action(APP_GO_SETTINGS,0); CHECK(!cs_app.menu.open && !cs_commands);
    cs_close();
}
static void grid(void)
{
    cs_open(false); cs_query("music"); cs_tick(250); cs_fixture("search");
    cs_action(APP_CATALOG_SECTION,SEARCH_ALBUMS); cs_tick(500);
    char body[8192]; size_t used=0;
    int n=snprintf(body,sizeof(body),"{\"data\":{\"searchV2\":{\"albumsV2\":{\"items\":["); CHECK(n>0); used=(size_t)n;
    for (unsigned i=0;i<12;++i) {
        n=snprintf(body+used,sizeof(body)-used,"%s{\"item\":{\"__typename\":\"AlbumResponseWrapper\",\"data\":{\"__typename\":\"Album\",\"uri\":\"spotify:album:00000000000000000000%02u\","
            "\"name\":\"Album %u\",\"playability\":{\"playable\":true},\"tracks\":{\"totalCount\":1}}}}",i ? "," : "",i+1,i+1);
        CHECK(n>0 && (size_t)n<sizeof(body)-used); used+=(size_t)n;
    }
    CHECK(snprintf(body+used,sizeof(body)-used,"],\"totalCount\":12,\"pagingInfo\":{\"nextOffset\":null}}}}}")>0);
    cs_response(body,200); CHECK(cs_app.catalog.page && cs_app.catalog.page->count==12);
    const float widths[]={400,1100,1440}; const float dpi[]={96,120,144,192};
    for (unsigned width=0;width<3;++width) { for (unsigned scale=0;scale<4;++scale) {
        (void)cs_layout(widths[width]*dpi[scale]/96,720*dpi[scale]/96,dpi[scale]);
        CHECK(app_catalog_cards(&cs_app)); uint32_t columns=app_catalog_columns(&cs_app);
        cs_app.widgets.focus=CLAY_ID("CatalogList").id;
        CHECK(app_key(&cs_app,APP_KEY_HOME,false)==APP_OK);
        CHECK(app_key(&cs_app,APP_KEY_RIGHT,false)==APP_OK && cs_app.widgets.lists[0].focus==app_catalog_key(&cs_app,1));
        CHECK(app_key(&cs_app,APP_KEY_DOWN,false)==APP_OK && cs_app.widgets.lists[0].focus==app_catalog_key(&cs_app,1+columns));
        CHECK(app_key(&cs_app,APP_KEY_END,false)==APP_OK && cs_app.widgets.lists[0].focus==app_catalog_key(&cs_app,11));
        (void)cs_layout(widths[width]*dpi[scale]/96,720*dpi[scale]/96,dpi[scale]);
        CHECK(Clay_GetElementData(CLAY_IDI("CatalogCard",11)).found);
        CHECK(cs_app.catalog.rendered<=12 && cs_app.widgets.lists[0].scroll>0);
        Clay_ElementData list=Clay_GetElementData(CLAY_ID("CatalogList")), player=Clay_GetElementData(CLAY_ID("AppNowPlaying"));
        CHECK(list.found && player.found && list.boundingBox.y+list.boundingBox.height<=player.boundingBox.y);
    } }
    CHECK(app_key(&cs_app,APP_KEY_ENTER,false)==APP_OK); cs_tick(500); cs_tick(500);
    CHECK(cs_app.model.location.screen==APP_ALBUM && !cs_commands);
    cs_action(APP_GO_SETTINGS,0); cs_close();
}
static void menu_pointer(void)
{
    cs_open(false); cs_query("music"); cs_tick(250); cs_fixture("search");
    (void)cs_layout(1100,720,96); focus_item(2);
    CHECK(app_context_open(&cs_app,0,0,true)==APP_OK); (void)cs_layout(1100,720,96);
    Clay_ElementData item=Clay_GetElementData(CLAY_IDI("RouteMenuItem",1)); CHECK(item.found);
    UiWidgetInput input={.x=item.boundingBox.x+12,.y=item.boundingBox.y+12,.down=true,.pressed=true};
    (void)app_layout(&cs_app,1100,720,96,input); CHECK(cs_app.menu.open && !cs_app.action_count);
    input.down=input.pressed=false; input.released=true;
    (void)app_layout(&cs_app,1100,720,96,input);
    CHECK(!cs_app.menu.open && cs_app.action_count==1 && !cs_commands);
    cs_tick(250); CHECK(cs_app.model.location.screen==APP_ALBUM); cs_fixture("album");
    (void)cs_layout(1100,720,96);
    Clay_ElementData play=Clay_GetElementData(CLAY_ID("CatalogPlay")); CHECK(play.found);
    (void)app_layout(&cs_app,1100,720,96,(UiWidgetInput){.x=play.boundingBox.x+12,.y=play.boundingBox.y+12});
    CHECK(Clay_GetElementData(CLAY_ID("ControlHint")).found);
    CHECK(cs_scheduler.stats.animation_ticks==0);
    cs_action(APP_GO_SETTINGS,0); cs_close();
}
void cs_interactions(void) { menus(); grid(); menu_pointer(); }
