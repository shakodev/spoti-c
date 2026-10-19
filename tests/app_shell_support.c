#include "app_shell_support.h"
#include <stdio.h>
#include <wchar.h>

App app;
CollectionService *collection;
JsonPagePool *pages;
AppRequest latest;
unsigned requests, cancellations, authentications;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static _Alignas(16) unsigned char models[COLLECTION_MODEL_BUDGET], pool[MEMORY_BUDGET_API_PAGES];
static UiScheduler scheduler;
static AppResult request(void *context, const AppRequest *r)
{
    (void)context; CHECK(!app.widgets.in_frame); latest=*r; ++requests; return APP_PENDING;
}
static void cancel(void *context, const AppRequest *r)
{
    (void)context; CHECK(!app.widgets.in_frame && r->serial); ++cancellations;
}
static AppResult auth(void *context, AppAuth action)
{
    (void)context; CHECK(!app.widgets.in_frame && (action==APP_LOGIN || action==APP_LOGOUT)); ++authentications; return APP_PENDING;
}
void shell_open(Player *player)
{
    requests=cancellations=authentications=0;
    if (!pages) { CHECK(json_page_pool_init(pool,sizeof(pool),&pages)==JSON_OK); }
    CHECK(json_page_pool_resident(pages)==0);
    CHECK(collection_service_init(models,sizeof(models),pages,&collection)==COLLECTION_OK);
    ui_scheduler_init(&scheduler); ui_scheduler_visibility(&scheduler,true,false,0);
    AppServices services={.request=request,.cancel=cancel,.auth=auth,.player=player,.demo=true};
    CHECK(app_init(&app,&services,&scheduler,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
}
void shell_close(void)
{
    uint64_t generation=app.model.generation;
    app_dispose(&app); CHECK(collection_reset(collection,generation+2)==COLLECTION_OK);
    CHECK(collection_resident_pages(collection)==0 && json_page_pool_resident(pages)==0);
}
void shell_action(AppActionType type)
{
    CHECK(app_enqueue(&app,(AppAction){.type=type})==APP_OK); (void)app_update(&app,100);
}
const CollectionPage *shell_page(AppRequest r, const char *name)
{
    CollectionRequest request;
    CHECK(collection_fetch(collection,r.kind,r.cursor,r.generation,&request)==COLLECTION_PENDING);
    char path[512]; CHECK(snprintf(path,sizeof(path),"%s/%s.json",APP_FIXTURE_DIR,name)>0);
    FILE *f=NULL; CHECK(fopen_s(&f,path,"rb")==0 && f);
    size_t bytes=fread(request.body,1,request.capacity,f); CHECK(!ferror(f) && fgetc(f)==EOF && fclose(f)==0);
    const CollectionPage *page=NULL;
    CollectionResult result=collection_complete(collection,request.ticket,COLLECTION_OK,200,bytes,&page);
    if (result!=COLLECTION_OK && result!=COLLECTION_PARTIAL && result!=COLLECTION_BOUNDARY) {
        fprintf(stderr,"fixture %s generation=%llu cursor=%u result=%u\n",name,(unsigned long long)r.generation,r.cursor,(unsigned)result);
    }
    CHECK(result==COLLECTION_OK || result==COLLECTION_PARTIAL || result==COLLECTION_BOUNDARY);
    CHECK(page && page->json.generation==r.generation); return page;
}
void shell_deliver(const char *name)
{
    AppRequest r=latest; const CollectionPage *page=shell_page(r,name);
    CHECK(app_deliver(&app,r,COLLECTION_OK,page)==APP_OK);
}
Clay_RenderCommandArray shell_layout(float width, float height, float dpi)
{
    Clay_RenderCommandArray commands=app_layout(&app,width,height,dpi,(UiWidgetInput){.x=-10,.y=-10});
    CHECK(commands.length>0 && app.widgets.error==UI_WIDGET_OK); return commands;
}
static FontCache fonts;
static UiRenderer renderer;
static UiRenderFrame render_frame;
int shell_visual(void)
{
    const float widths[]={500,1375,1800}, heights[]={800,900,1125};
    const char *names[]={"narrow","default","wide"};
    CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    FontMeasureContext text={.cache=&fonts,.config={.dpi=120,.weight=400,.max_width=260,.ellipsis=true}};
    app_bind_fonts(&app,&text); app.model.detail=true;
    for (unsigned i=0;i<3;++i) {
        UiRendererConfig config={.width=(uint32_t)widths[i],.height=(uint32_t)heights[i],.warp=true};
        UiRenderResult result=ui_renderer_create(&renderer,&config);
        if (result==UI_RENDER_UNSUPPORTED) { font_cache_destroy(&fonts); app.fonts=NULL; return 77; }
        CHECK(result==UI_RENDER_OK);
        for (unsigned n=0;n<24;++n) {
            (void)font_prepare_pending(&fonts); CHECK(font_upload_renderer(&fonts,&renderer)==FONT_OK);
            Clay_RenderCommandArray commands=shell_layout(widths[i],heights[i],120);
            CHECK(ui_frame_begin(&render_frame,config.width,config.height,1.25f)==UI_RENDER_OK);
            UiClayBridge bridge={.custom=app_render_custom,.context=&app};
            CHECK(ui_frame_clay(&render_frame,commands,&bridge)==UI_RENDER_OK);
            const float clear[4]={0,0,0,1}; CHECK(ui_renderer_submit(&renderer,&render_frame,clear)==UI_RENDER_OK);
        }
        size_t bytes=(size_t)config.width*config.height*4;
        CHECK(font_stats(&fonts).pending==0);
        unsigned char *rgba=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(rgba);
        CHECK(ui_renderer_readback(&renderer,rgba,(size_t)config.width*4,bytes)==UI_RENDER_OK);
        char path[100]; CHECK(snprintf(path,sizeof(path),"app-shell-%s.rgba",names[i])>0);
        FILE *f=NULL; CHECK(fopen_s(&f,path,"wb")==0 && f);
        CHECK(fwrite(rgba,1,bytes,f)==bytes && fclose(f)==0); CHECK(VirtualFree(rgba,0,MEM_RELEASE));
        printf("visual=%s %ux%u\n",names[i],config.width,config.height);
        /* Explicit atlas reset releases the renderer-owned texture first. */
        font_cache_destroy(&fonts); ui_renderer_destroy(&renderer);
        if (i<2) { memset(&fonts,0,sizeof(fonts)); CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK); app_bind_fonts(&app,&text); }
    }
    app.fonts=NULL; return 0;
}
