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
