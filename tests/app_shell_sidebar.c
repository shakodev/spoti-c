#include "app_shell_support.h"
#include "support/ui_heap_watch.h"
#include "ui/font_roles.h"
#include "ui/icons_internal.h"
#include <math.h>
#include <stdio.h>

static AppRequest sidebar_request;
static unsigned sidebar_requests;
static _Alignas(16) unsigned char rootlist_models[COLLECTION_MODEL_BUDGET];
static AppRootlistStore *rootlists;
static bool same_box(Clay_BoundingBox a,Clay_BoundingBox b)
{
    return fabsf(a.x-b.x)<0.01f && fabsf(a.y-b.y)<0.01f &&
        fabsf(a.width-b.width)<0.01f && fabsf(a.height-b.height)<0.01f;
}
static const Clay_RenderCommand *text_command(Clay_RenderCommandArray commands,const char *text)
{
    size_t bytes=strlen(text);
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT && c->renderData.text.stringContents.length==(int32_t)bytes &&
            !memcmp(c->renderData.text.stringContents.chars,text,bytes)) { return c; }
    }
    return NULL;
}
static unsigned border_width(Clay_RenderCommandArray commands,Clay_BoundingBox box)
{
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_BORDER && same_box(c->boundingBox,box)) { return c->renderData.border.width.left; }
    }
    return 0;
}
static void filled_surface(Clay_RenderCommandArray commands,Clay_BoundingBox box)
{
    bool found=false;
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_RECTANGLE && same_box(c->boundingBox,box)) {
            CHECK(c->renderData.rectangle.backgroundColor.a==255 && c->renderData.rectangle.backgroundColor.r==31);
            CHECK(c->renderData.rectangle.cornerRadius.topLeft==24); found=true;
        }
    }
    CHECK(found);
}
static void header_surface(void)
{
    shell_open(NULL); app.services.demo=false; app.widgets.focus=CLAY_ID("NavAuth").id;
    const float widths[]={400,1100,1800},dpis[]={96,120,144,192};
    for (unsigned d=0;d<4;++d) {
        for (unsigned w=0;w<3;++w) {
            Clay_RenderCommandArray commands=shell_layout(widths[w]*dpis[d]/96,900*dpis[d]/96,dpis[d]);
            Clay_BoundingBox search=Clay_GetElementData(CLAY_ID("NavSearch")).boundingBox;
            Clay_BoundingBox icon=Clay_GetElementData(CLAY_ID("NavSearchIcon")).boundingBox;
            Clay_BoundingBox home=Clay_GetElementData(CLAY_ID("NavHome")).boundingBox;
            CHECK(search.height==48 && icon.width==24 && fabsf(icon.x-search.x-12)<0.01f);
            CHECK(fabsf(icon.y+icon.height/2-search.y-search.height/2)<0.01f);
            const Clay_RenderCommand *placeholder=text_command(commands,"What do you want to play?"); CHECK(placeholder);
            CHECK(placeholder->renderData.text.fontId==FONT_ROLE_BODY && placeholder->renderData.text.fontSize==16);
            CHECK(placeholder->renderData.text.textColor.r==179 && fabsf(placeholder->boundingBox.x-search.x-48)<0.01f);
            CHECK(!border_width(commands,search)); filled_surface(commands,search); filled_surface(commands,home);
        }
    }
    app.model.location.screen=APP_SEARCH;
    Clay_RenderCommandArray commands=shell_layout(1100,900,96);
    Clay_BoundingBox search=Clay_GetElementData(CLAY_ID("NavSearch")).boundingBox; unsigned glyphs=0;
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_CUSTOM && c->userData==&ui_icon_command_tag &&
            c->boundingBox.x>=search.x && c->boundingBox.x<search.x+search.width &&
            c->boundingBox.y>=search.y && c->boundingBox.y<search.y+search.height) {
            const UiIconCommand *icon=c->renderData.custom.customData;
            CHECK(icon->icon==UI_ICON_SEARCH); ++glyphs;
        }
    }
    CHECK(glyphs==1 && !border_width(commands,search));
    app.widgets.focus=CLAY_ID("NavSearch").id; commands=shell_layout(1100,900,96);
    CHECK(border_width(commands,Clay_GetElementData(CLAY_ID("NavSearch")).boundingBox)==2);
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK && app.action_count==1 && app.actions[0].type==APP_GO_SEARCH);
    app.action_count=0;
    UiWidgetInput press={.x=search.x+72,.y=search.y+24,.down=true,.pressed=true};
    (void)app_layout(&app,1100,900,96,press);
    CHECK(app.widgets.active==CLAY_ID("NavSearch").id && !app.action_count);
    press.down=press.pressed=false; press.released=true; (void)app_layout(&app,1100,900,96,press);
    CHECK(app.action_count==1 && app.actions[0].type==APP_GO_SEARCH); app.action_count=0;
    app.services.demo=true;
    commands=shell_layout(1100,900,96); CHECK(text_command(commands,"What do you want to play? | Offline demo"));
    commands=shell_layout(400,900,96); CHECK(text_command(commands,"Offline demo"));
    shell_close();
}
static const AppRootlistPage *page_for(AppRequest r)
{
    AppRootlistPage *page=app_rootlist_begin(rootlists,r.generation,r.cursor,4); CHECK(page);
    const char *uri="spotify:playlist:0000000000000000000003",*name=r.cursor ? "Next playlist" : "Evening rotation";
    CHECK(app_rootlist_append(page,(JsonString){uri,strlen(uri)},(JsonString){name,strlen(name)},(JsonString){0},true));
    app_rootlist_publish(page,r.cursor ? COLLECTION_END : 2); return page;
}
static AppResult request(void *context,const AppRequest *r)
{
    (void)context; CHECK(r->sidebar && r->kind==COLLECTION_LIBRARY && r->screen==APP_LIBRARY);
    CHECK(!app.widgets.in_frame); sidebar_request=*r; ++sidebar_requests; return APP_PENDING;
}
static void open_sidebar(void)
{
    shell_open(NULL); sidebar_requests=0; app.services.sidebar=request;
    rootlists=app_rootlist_store(rootlist_models,sizeof(rootlist_models)); CHECK(rootlists);
    CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    (void)app_update(&app,1); CHECK(sidebar_requests==1 && app.sidebar.pending);
    const AppRootlistPage *page=page_for(sidebar_request);
    CHECK(app_sidebar_deliver(&app,sidebar_request,COLLECTION_OK,page)==APP_OK);
    CHECK(app.sidebar.page==page && !app.sidebar.pending && app.model.page);
}
static void ownership(void)
{
    open_sidebar(); const AppRootlistPage *saved=app.sidebar.page;
    (void)shell_layout(1100,720,96); CHECK(app.sidebar.count==1 && app.sidebar.visible_count==1);
    shell_action(APP_GO_SETTINGS); CHECK(app.sidebar.page==saved && !app.model.page);
    CHECK(!strcmp(saved->items[0].name,"Evening rotation"));
    CHECK(collection_reset(collection,app.model.generation)==COLLECTION_OK);
    CHECK(collection_resident_pages(collection)==0 && json_page_pool_resident(pages)==0);
    (void)shell_layout(1100,720,96); CHECK(app.sidebar.count==1);
    CHECK(ui_text_set(&app.widgets.texts[2],"ROTATION",8)==UI_WIDGET_OK); shell_action(APP_SIDEBAR_QUERY);
    (void)shell_layout(1100,720,96); CHECK(app.sidebar.count==1);
    CHECK(ui_text_set(&app.widgets.texts[2],"missing",7)==UI_WIDGET_OK); shell_action(APP_SIDEBAR_QUERY);
    (void)shell_layout(1100,720,96); CHECK(app.sidebar.count==0 && app.sidebar.page==saved);
    CHECK(ui_text_set(&app.widgets.texts[2],"",0)==UI_WIDGET_OK);
    (void)shell_layout(1100,720,96); app.widgets.focus=CLAY_ID("SidebarList").id;
    CHECK(app_key(&app,APP_KEY_END,false)==APP_OK && app.widgets.lists[1].focus==1);
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK && app.action_count==1);
    CHECK(app.actions[0].type==APP_SIDEBAR_SELECT && !strcmp(app.actions[0].entity.title,"Evening rotation"));
    app.action_count=0;
    shell_action(APP_SIDEBAR_TOGGLE); (void)shell_layout(1100,720,96);
    CHECK(app.geometry.sidebar==64 && app.sidebar.visible_count==1 && Clay_GetElementData(CLAY_IDI("SidebarItem",1)).found);
    CHECK(sidebar_requests==1); shell_close();
}
static void stale_and_failure(void)
{
    open_sidebar(); const AppRootlistPage *saved=app.sidebar.page;
    shell_action(APP_SIDEBAR_MORE); CHECK(sidebar_requests==2 && sidebar_request.cursor==2 && app.sidebar.pending);
    AppRequest old=sidebar_request; const AppRootlistPage *page=page_for(old);
    shell_action(APP_GO_SETTINGS); CHECK(!app.sidebar.pending && app.sidebar.page==saved && cancellations>0);
    CHECK(app_sidebar_deliver(&app,old,COLLECTION_OK,page)==APP_STALE && app.sidebar.page==saved);
    shell_action(APP_SIDEBAR_MORE); CHECK(app_sidebar_deliver(&app,sidebar_request,COLLECTION_ERROR,NULL)==APP_FAILED);
    CHECK(app.sidebar.failed && app.sidebar.page==saved);
    unsigned count=sidebar_requests; for (unsigned i=0;i<10;++i) { (void)app_update(&app,i); }
    CHECK(sidebar_requests==count); shell_action(APP_SIDEBAR_RETRY);
    CHECK(app.sidebar.pending && sidebar_requests==count+1);
    old=sidebar_request; page=page_for(old);
    shell_action(APP_SIGN_OUT); CHECK(!app.sidebar.page && !app.sidebar.pending && !app.widgets.texts[2].length);
    CHECK(app_sidebar_deliver(&app,old,COLLECTION_OK,page)==APP_STALE && !app.sidebar.page);
    shell_close();
}
static void menu_and_geometry(void)
{
    open_sidebar(); shell_action(APP_GO_SETTINGS);
    const float widths[]={400,760,1100,1600};
    for (unsigned i=0;i<4;++i) {
        (void)shell_layout(widths[i],900,96);
        Clay_BoundingBox center=Clay_GetElementData(CLAY_ID("HeaderCenter")).boundingBox;
        Clay_BoundingBox avatar=Clay_GetElementData(CLAY_ID("NavAuth")).boundingBox;
        CHECK(fabsf(center.x+center.width/2-widths[i]/2)<0.01f && avatar.x+avatar.width<=widths[i]-8);
    }
    shell_action(APP_PROFILE_TOGGLE); CHECK(app.sidebar.profile);
    Clay_RenderCommandArray menu=shell_layout(1100,720,96);
    CHECK(app.control_count==7 && app.widgets.focus==CLAY_IDI("ProfileItem",0).id);
    const Clay_RenderCommand *account=text_command(menu,"Account"); CHECK(account);
    Clay_BoundingBox account_row=Clay_GetElementData(CLAY_IDI("ProfileItem",0)).boundingBox;
    CHECK(account->renderData.text.fontId==FONT_ROLE_BODY && account->renderData.text.fontSize==14);
    CHECK(fabsf(account->boundingBox.x-account_row.x-12)<0.01f && border_width(menu,account_row)==2);
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,1);
    CHECK(!app.sidebar.profile && strstr(app.model.notice,"Account service is unavailable"));
    (void)shell_layout(1100,720,96); CHECK(app.widgets.focus==CLAY_ID("NavAuth").id);
    shell_action(APP_PROFILE_TOGGLE); (void)shell_layout(1100,720,96);
    CHECK(app_key(&app,APP_KEY_UP,false)==APP_OK && app.widgets.focus==CLAY_IDI("ProfileItem",6).id);
    CHECK(app_key(&app,APP_KEY_ESCAPE,false)==APP_OK); (void)app_update(&app,2);
    CHECK(!app.sidebar.profile && app.widgets.focus==CLAY_ID("NavAuth").id);
    shell_action(APP_PROFILE_TOGGLE); (void)shell_layout(1100,720,96);
    UiWidgetInput input=app_profile_input(&app,(UiWidgetInput){.x=400,.y=450,.down=true,.pressed=true});
    CHECK(!app.sidebar.profile && input.cancel && !input.pressed && !input.down);
    shell_action(APP_PROFILE_TOGGLE); (void)shell_layout(1100,720,96);
    app.widgets.focus=CLAY_IDI("ProfileItem",5).id;
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,3);
    CHECK(!app.sidebar.profile && app.model.location.screen==APP_SETTINGS);
    for (unsigned i=0;i<5;++i) { (void)shell_layout(1100,720,96); (void)app_update(&app,i); }
    ui_heap_begin();
    for (unsigned i=0;i<100;++i) { (void)shell_layout(1100,720,96); (void)app_update(&app,i); }
    ui_heap_end(); CHECK(sidebar_requests==1);
    shell_action(APP_PROFILE_TOGGLE); (void)shell_layout(1100,720,96); app.widgets.focus=CLAY_IDI("ProfileItem",6).id;
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,4);
    CHECK(!app.sidebar.profile && !app.sidebar.page && !app.model.logged_in && authentications==1);
    shell_close();
}
static void dense_and_capacity(void)
{
    open_sidebar(); shell_action(APP_SIDEBAR_RETRY);
    AppRootlistPage *page=app_rootlist_begin(rootlists,sidebar_request.generation,0,32); CHECK(page);
    CHECK(!app_rootlist_begin(rootlists,sidebar_request.generation,0,32));
    for (unsigned i=0;i<APP_ROOTLIST_ROWS;++i) {
        char uri[192],name[192]; CHECK(sprintf_s(uri,sizeof(uri),"spotify:playlist:%022u",i+1)>0);
        CHECK(sprintf_s(name,sizeof(name),"Saved playlist %u",i+1)>0);
        CHECK(app_rootlist_append(page,(JsonString){uri,strlen(uri)},(JsonString){name,strlen(name)},(JsonString){0},true));
    }
    CHECK(!app_rootlist_append(page,(JsonString){0},(JsonString){0},(JsonString){0},true));
    app_rootlist_publish(page,16);
    CHECK(app_sidebar_deliver(&app,sidebar_request,COLLECTION_OK,page)==APP_OK);
    shell_action(APP_RETRY); shell_dense_page(true);
    (void)shell_layout(1600,900,96); CHECK(app.sidebar.count==16 && app.sidebar.visible_count>1 && app.browse.visible_count>1);
    CHECK(app.sidebar.visible_count<=10 && app.control_count<=APP_CONTROLS && app.label_count<=64);
    app.widgets.input=(UiWidgetInput){.x=30,.y=400}; double main=app.widgets.lists[0].scroll;
    app_scroll(&app,256); (void)shell_layout(1600,900,96);
    CHECK(app.widgets.lists[1].scroll>0 && app.widgets.lists[0].scroll==main);
    app.widgets.focus=CLAY_ID("SidebarList").id; CHECK(app_key(&app,APP_KEY_END,false)==APP_OK);
    CHECK(app.widgets.lists[1].focus==16); (void)shell_layout(1600,900,96);
    CHECK(Clay_GetElementData(CLAY_IDI("SidebarItem",16)).found);
    shell_action(APP_PROFILE_TOGGLE); (void)shell_layout(1600,900,96);
    CHECK(app.control_count==7 && Clay_GetElementData(CLAY_ID("ProfileMenu")).found);
    shell_close();
}
static _Alignas(16) unsigned char async_core[MEMORY_BUDGET_CORE];
static AppFixture host;
static void asynchronous_rootlist(void)
{
    Arena core={0}; EventQueue *events=NULL;
    CHECK(arena_init(&core,async_core,sizeof(async_core)) && event_queue_create(&core,16,&events)==CORE_OK);
    shell_open(NULL); app_dispose(&app);
    CHECK(app_fixture_init(&host,&core,events,&app,L"" APP_FIXTURE_DIR)==APP_OK);
    AppServices services=app_fixture_services(&host);
    static _Alignas(64) unsigned char clay[UI_CLAY_BYTES]; static unsigned char scratch[UI_SCRATCH_BYTES];
    CHECK(app_init(&app,&services,NULL,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    CHECK(app_session(&app,true,true,0)==APP_PENDING);
    for (unsigned i=0;i<40 && (!app.model.page || !app.sidebar.page);++i) {
        (void)app_update(&app,i); (void)WaitForSingleObject(event_wait_handle(events),100);
        Event batch[16]; size_t n; CHECK(event_drain(events,batch,16,&n)==CORE_OK); app_fixture_pump(&host);
    }
    CHECK(app.model.page && app.model.page->kind==COLLECTION_HOME && app.sidebar.page && app.sidebar.page->count==1);
    const AppRootlistPage *saved=app.sidebar.page;
    shell_action(APP_GO_LIBRARY);
    for (unsigned i=0;i<40 && !app.model.page;++i) {
        (void)WaitForSingleObject(event_wait_handle(events),100);
        Event batch[16]; size_t n; CHECK(event_drain(events,batch,16,&n)==CORE_OK); app_fixture_pump(&host);
    }
    CHECK(app.model.page && app.model.page->kind==COLLECTION_LIBRARY && app.sidebar.page==saved);
    shell_action(APP_MORE);
    for (unsigned i=0;i<40 && app.model.pending;++i) {
        (void)WaitForSingleObject(event_wait_handle(events),100);
        Event batch[16]; size_t n; CHECK(event_drain(events,batch,16,&n)==CORE_OK); app_fixture_pump(&host);
    }
    CHECK(app.model.page && app.model.page->offset==2 && app.sidebar.page==saved);
    CHECK(!strcmp(saved->items[0].name,"Evening rotation"));
    app_dispose(&app); CHECK(app_fixture_stop(&host));
    CHECK(event_queue_close(events)==CORE_OK); Event batch[16]; size_t n;
    (void)event_drain(events,batch,16,&n); CHECK(event_queue_destroy(events)==CORE_OK); arena_dispose(&core);
}
void shell_sidebar(void)
{
    printf("persistent shell state: App=%zu, collection+rootlist=%zu (caps 262144/65536 bytes)\n",
        sizeof(App),collection_service_size()+sizeof(AppRootlistStore));
    header_surface(); ownership(); stale_and_failure(); menu_and_geometry(); dense_and_capacity(); asynchronous_rootlist();
    puts("persistent shell: retained rootlist, stale account delivery, loaded-page search, collapse, keyboard, menu and warm allocation checks passed");
}
