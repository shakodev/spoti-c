#include "app_shell_support.h"

static JsonString span(const char *text) { return (JsonString){text,strlen(text)}; }
static void filter(uint32_t value)
{
    CHECK(app_enqueue(&app,(AppAction){.type=APP_LIBRARY_FILTER,.value=value})==APP_OK);
    CHECK(app_update(&app,100)==APP_OK);
}
static void query(const char *text)
{
    CHECK(ui_text_set(&app.widgets.texts[1],text,(uint32_t)strlen(text))==UI_WIDGET_OK);
    shell_action(APP_LIBRARY_QUERY); CHECK(app.model.last_result==APP_OK);
}
static void expect_order(const uint16_t *order,uint32_t count)
{
    app_browse_project(&app); CHECK(app.browse.count==count);
    CHECK(!memcmp(app.browse.order,order,count*sizeof(*order)));
}
static void projection(void)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING);
    shell_action(APP_GO_LIBRARY); shell_deliver("library");
    const CollectionPage *owned=app.model.page;
    /* This fabricated immutable page is projection-only: never deliver, retain,
     * update, layout or dispose it through the application/page service. */
    CollectionPage page={.kind=COLLECTION_LIBRARY,.count=7};
    const char *names[]={"MiXeD Playlist","Bj\xc3\xb6rk \xf0\x9f\x8e\xb5",
        "\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a","Liked MIXED","Mixed folder",
        "Mixed album","MIXED artist"};
    const CollectionItemKind kinds[]={COLLECTION_PLAYLIST_ITEM,COLLECTION_ALBUM,
        COLLECTION_ARTIST,COLLECTION_LIKED,COLLECTION_FOLDER,COLLECTION_ALBUM,COLLECTION_ARTIST};
    for (uint32_t i=0;i<page.count;++i) {
        page.items[i]=(CollectionItem){.kind=kinds[i],.name=span(names[i])};
    }
    AppLocation saved=app.model.location; app.model.page=&page;
    CHECK(strcpy_s(app.model.location.query,sizeof(app.model.location.query),"mIxEd")==0);
    const uint16_t mixed[]={0,3,4,5,6}, playlists[]={0,3}, albums[]={5}, artists[]={6};
    expect_order(mixed,5);
    app.model.location.selector=1; expect_order(playlists,2);
    app.model.location.selector=2; expect_order(albums,1);
    app.model.location.selector=3; expect_order(artists,1);
    app.model.location.selector=0;
    const uint16_t unicode[]={1}, arabic[]={2};
    CHECK(strcpy_s(app.model.location.query,sizeof(app.model.location.query),"\xc3\xb6rk \xf0\x9f\x8e\xb5")==0);
    expect_order(unicode,1);
    CHECK(strcpy_s(app.model.location.query,sizeof(app.model.location.query),names[2])==0);
    expect_order(arabic,1);
    /* Non-ASCII comparisons are exact bytes; this is not locale case folding. */
    CHECK(strcpy_s(app.model.location.query,sizeof(app.model.location.query),"BJ\xc3\x96RK")==0);
    expect_order(unicode,0);
    app.model.page=owned; app.model.location=saved; app_browse_project(&app);
    shell_close();
}
static void long_name_identity(void)
{
    char name[256]; memset(name,'a',187);
    memcpy(name+187,"\xf0\x9f\x8e\xb5",4); memset(name+191,'b',64); name[255]=0;
    CollectionItem item={.kind=COLLECTION_TRACK,.uri=span("spotify:track:0000000000000000000001"),
        .name={name,255},.available=true,.has_compact_id=true,.compact_id={1,2,3,4}};
    AppEntity entity;
    CHECK(app_entity(&item,&entity)==APP_OK && entity.playable && entity.kind==COLLECTION_TRACK);
    CHECK(!strcmp(entity.uri,item.uri.data) && !memcmp(entity.id,item.compact_id,16));
    CHECK(strlen(entity.title)==190 && !memcmp(entity.title,name,187));
    CHECK(!strcmp(entity.title+187,"..."));
    wchar_t wide[192];
    CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,entity.title,-1,wide,192)>0);
    /* A four-byte character ending exactly at the truncation boundary stays. */
    memset(name,'a',184); memcpy(name+184,"\xf0\x9f\x8e\xb5",4); memset(name+188,'b',67);
    CHECK(app_entity(&item,&entity)==APP_OK && strlen(entity.title)==191);
    CHECK(!memcmp(entity.title,name,188) && !strcmp(entity.title+188,"..."));
    CHECK(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,entity.title,-1,wide,192)>0);
}
static void restore_and_replace(void)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING);
    shell_action(APP_GO_LIBRARY); shell_deliver("library");
    query("Bj\xc3\xb6rk"); filter(2); shell_action(APP_LIBRARY_GRID);
    CHECK(app.model.location.grid && app.browse.count==1 && app.browse.order[0]==1);
    app.widgets.lists[0].scroll=23; shell_action(APP_GO_SETTINGS);
    CHECK(app.model.location.screen==APP_SETTINGS && !app.model.page);
    shell_action(APP_BACK);
    CHECK(app.model.location.screen==APP_LIBRARY && app.model.location.grid);
    CHECK(app.model.location.selector==2 && !strcmp(app.model.location.query,"Bj\xc3\xb6rk"));
    CHECK(app.widgets.texts[1].length==6 && !strcmp(app.widgets.texts[1].text,app.model.location.query));
    CHECK(app.widgets.lists[0].scroll==23); shell_deliver("library");
    app_browse_project(&app); CHECK(app.browse.count==1 && app.browse.order[0]==1);
    query(""); filter(0);
    const CollectionPage *old=app.model.page; CHECK(page_retain((JsonPage *)&old->json));
    app.browse.previous[0]=(AppVisibleImage){UINT32_MAX,1,128};
    app.browse.visible[0]=app.browse.previous[0];
    app.browse.previous_count=app.browse.visible_count=1;
    app.browse.generation=app.model.generation; app.image_count=1;
    uint64_t generation=app.model.generation;
    shell_action(APP_MORE); CHECK(app.model.page==old && latest.cursor==2);
    shell_deliver("library-next");
    CHECK(app.model.generation==generation && app.model.page!=old && app.model.page->offset==2);
    CHECK(!app.browse.previous_count && !app.browse.visible_count && !app.image_count);
    CHECK(collection_resident_pages(collection)==2 && json_page_pool_resident(pages)==2);
    CHECK(old->items[0].name.bytes==16 && !memcmp(old->items[0].name.data,"Evening rotation",16));
    CHECK(page_release((JsonPage *)&old->json));
    CHECK(collection_resident_pages(collection)==1 && json_page_pool_resident(pages)==1);
    (void)shell_layout(1100,720,96); (void)ui_scheduler_frame(app.scheduler,100);
    for (unsigned i=0;i<200;++i) {
        (void)shell_layout(1100,720,96);
        CHECK(ui_scheduler_frame(app.scheduler,100)==0);
        CHECK(ui_scheduler_deadline(app.scheduler)==CORE_CLOCK_NEVER);
    }
    shell_close();
}
static unsigned wakes;
static void wake(void *context) { CHECK(context==&wakes); ++wakes; }
static void visible_notifications(void)
{
    shell_open(NULL); wakes=0;
    app.services.wake=wake; app.services.context=&wakes;
    /* Presence token only: end() tests whether a service is attached but never
     * dereferences it. Restore NULL before any layout/update/dispose/service API. */
    static _Alignas(16) unsigned char presence[16];
    app.services.artwork=(Artwork *)(void *)presence;
    (void)ui_scheduler_frame(app.scheduler,0);
    app_browse_begin(&app);
    app.browse.visible[app.browse.visible_count++]=(AppVisibleImage){UINT32_MAX,4,64};
    app_browse_end(&app);
    CHECK(wakes==1 && ui_scheduler_frame(app.scheduler,0)==UI_DIRTY_STATE);
    for (unsigned i=0;i<200;++i) {
        app_browse_begin(&app);
        app.browse.visible[app.browse.visible_count++]=(AppVisibleImage){UINT32_MAX,4,64};
        app_browse_end(&app);
        CHECK(wakes==1 && ui_scheduler_frame(app.scheduler,0)==0);
        CHECK(ui_scheduler_deadline(app.scheduler)==CORE_CLOCK_NEVER);
    }
    app.browse.visible[0].index=5; app_browse_end(&app);
    CHECK(wakes==2); (void)ui_scheduler_frame(app.scheduler,0);
    app.browse.visible[0].size=128; app_browse_end(&app);
    CHECK(wakes==3); (void)ui_scheduler_frame(app.scheduler,0);
    app.browse.visible[0].shelf=2; app_browse_end(&app);
    CHECK(wakes==4); (void)ui_scheduler_frame(app.scheduler,0);
    ++app.model.generation; app_browse_end(&app);
    CHECK(wakes==5); (void)ui_scheduler_frame(app.scheduler,0);
    app_browse_begin(&app); app_browse_end(&app);
    CHECK(wakes==6); (void)ui_scheduler_frame(app.scheduler,0);
    app_browse_end(&app); CHECK(wakes==6 && ui_scheduler_frame(app.scheduler,0)==0);
    app.services.artwork=NULL;
    app.browse.visible[app.browse.visible_count++]=(AppVisibleImage){UINT32_MAX,7,64};
    app_browse_end(&app);
    CHECK(wakes==6 && ui_scheduler_frame(app.scheduler,0)==0);
    app.services.wake=NULL; app.services.context=NULL; shell_close();
}
static void keyboard_page(void)
{
    const char *body="{\"statusCode\":200,\"header\":{\"totalCount\":20},\"entity\":["
        "{\"entityInfo\":{\"name\":\"Other\",\"uri\":\"fixture:0\"},\"playlist\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep one\",\"uri\":\"fixture:1\"},\"album\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep artist\",\"uri\":\"fixture:2\"},\"artist\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep two\",\"uri\":\"fixture:3\"},\"album\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep folder\",\"uri\":\"fixture:4\"},\"folder\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep three\",\"uri\":\"fixture:5\"},\"album\":{}},"
        "{\"entityInfo\":{\"name\":\"Skip\",\"uri\":\"fixture:6\"},\"album\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep four\",\"uri\":\"fixture:7\"},\"album\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep five\",\"uri\":\"fixture:8\"},\"album\":{}},"
        "{\"entityInfo\":{\"name\":\"Keep six\",\"uri\":\"fixture:9\"},\"album\":{}}]}";
    CollectionRequest request; AppRequest delivery=latest;
    CHECK(collection_fetch(collection,delivery.kind,delivery.cursor,delivery.generation,&request)==COLLECTION_PENDING);
    CHECK(strlen(body)<request.capacity); memcpy(request.body,body,strlen(body));
    const CollectionPage *page=NULL;
    CHECK(collection_complete(collection,request.ticket,COLLECTION_OK,200,strlen(body),&page)==COLLECTION_OK);
    CHECK(app_deliver(&app,delivery,COLLECTION_OK,page)==APP_OK);
}
static void filtered_keyboard(void)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING);
    shell_action(APP_GO_LIBRARY); keyboard_page();
    shell_action(APP_MORE); CHECK(latest.cursor==10); keyboard_page();
    query("kEeP"); filter(2); shell_action(APP_LIBRARY_GRID);
    (void)shell_layout(900,720,96); CHECK(app_browse_columns(&app)==3);
    const uint16_t expected[]={1,3,5,7,8,9}; expect_order(expected,6);
    app.widgets.focus=CLAY_ID("AppList").id;
    CHECK(app_key(&app,APP_KEY_HOME,false)==APP_OK && app.widgets.lists[0].focus==12);
    CHECK(app_key(&app,APP_KEY_DOWN,false)==APP_OK && app.widgets.lists[0].focus==18);
    CHECK(app.widgets.lists[0].selection==18 && app.widgets.lists[0].scroll>0);
    CHECK(app_key(&app,APP_KEY_RIGHT,false)==APP_OK && app.widgets.lists[0].focus==19);
    CHECK(app_key(&app,APP_KEY_UP,false)==APP_OK && app.widgets.lists[0].focus==14);
    CHECK(app_key(&app,APP_KEY_END,false)==APP_OK && app.widgets.lists[0].focus==20);
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK && app.action_count==1);
    CHECK(app.actions[0].type==APP_SELECT && !strcmp(app.actions[0].entity.uri,"fixture:9"));
    CHECK(!strcmp(app.actions[0].entity.title,"Keep six")); app.action_count=0;
    /* Pointer focus hands the original index to the same keyboard projection. */
    app.widgets.focus=CLAY_IDI("LibraryItem",14).id;
    CHECK(app_key(&app,APP_KEY_RIGHT,false)==APP_OK && app.widgets.lists[0].focus==16);
    shell_close();
}
void shell_visibility(void)
{
    projection(); long_name_identity(); restore_and_replace(); visible_notifications(); filtered_keyboard();
    puts("browse: UTF-8 projection, filtered original keys, history, page replacement and idle visibility passed");
}
