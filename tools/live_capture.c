#include "live_capture.h"
#include "ui/app_artwork_host.h"
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_WIDTH 1100u
#define CAPTURE_HEIGHT 900u
#define CAPTURE_SETTLE_MS 30000u
static App app;
static AppArtworkHost artwork;
static FontCache fonts;
static FontMeasureContext text;
static UiRenderer renderer;
static UiRenderFrame frame;
static UiScheduler scheduler;
static Metrics metrics;
static MetricsArena core_metrics;
static Arena core;
static EventQueue *events;
static _Alignas(16) unsigned char core_memory[MEMORY_BUDGET_CORE];
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static unsigned char pixels[CAPTURE_WIDTH*CAPTURE_HEIGHT*4u];
static DWORD owner;
static bool renderer_ready,fonts_ready,core_ready,metrics_ready;

typedef struct CaptureCounts { unsigned visible,ready,failed,pending,missing; } CaptureCounts;
static CaptureCounts counts(void)
{
    CaptureCounts out={.visible=app.browse.visible_count};
    for (unsigned i=0;i<app.image_count;++i) {
        ArtResult result=app.images[i].result;
        if (result==ART_OK) { ++out.ready; }
        else if (result==ART_PENDING || result==ART_FULL) { ++out.pending; }
        else { ++out.failed; }
    }
    out.missing=out.visible>app.image_count ? out.visible-app.image_count : 0;
    return out;
}
static void wake(void *context)
{
    Event event={.type=UI_DIRTY_STATE}; (void)event_post(context,&event);
}
static bool drain(void)
{
    Event batch[16]; size_t count;
    return !events || event_drain(events,batch,16,&count)==CORE_OK;
}
static bool prepare_frame(void)
{
    if (!drain()) { return false; }
    (void)app_update(&app,GetTickCount64());
    if (font_prepare_pending(&fonts)!=FONT_OK || font_upload_renderer(&fonts,&renderer)!=FONT_OK) { return false; }
    Clay_RenderCommandArray commands=app_layout(&app,(float)CAPTURE_WIDTH,(float)CAPTURE_HEIGHT,96,
        (UiWidgetInput){.x=-1,.y=-1});
    if (!commands.length || app.widgets.error!=UI_WIDGET_OK ||
        ui_frame_begin(&frame,CAPTURE_WIDTH,CAPTURE_HEIGHT,1)!=UI_RENDER_OK) { return false; }
    UiClayBridge bridge={.custom=app_render_custom,.context=&app};
    return ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK;
}
static bool settle(ULONGLONG deadline,bool *complete)
{
    *complete=false;
    for (unsigned pass=0;pass<512 && GetTickCount64()<deadline;++pass) {
        if (!prepare_frame()) { return false; }
        Artwork *service=app.services.artwork;
        ArtworkStats art=service ? artwork_stats(service) : (ArtworkStats){0};
        CaptureCounts images=counts();
        if (pass>2 && !font_stats(&fonts).pending && !art.active && !art.pending && !images.pending) {
            *complete=true; return true;
        }
        if (!font_stats(&fonts).pending && art.active) {
            ULONGLONG now=GetTickCount64();
            if (now>=deadline) { break; }
            DWORD remaining=(DWORD)(deadline-now);
            if (remaining>1000) { remaining=1000; }
            DWORD waited=WaitForSingleObject(event_wait_handle(events),remaining);
            if (waited!=WAIT_OBJECT_0 && waited!=WAIT_TIMEOUT) { return false; }
        }
    }
    return true;
}
static bool name_valid(const char *name)
{
    if (!name || !*name) { return false; }
    size_t length=0;
    for (;length<64 && name[length];++length) {
        unsigned char c=(unsigned char)name[length];
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') ||
            (c>='0' && c<='9') || c=='-' || c=='_')) { return false; }
    }
    return length<64;
}
static bool save(const char *directory,const char *name,bool grid,bool complete,CaptureCounts image)
{
    char path[MAX_PATH],hash[65]; unsigned char digest[32];
    const char hex[]="0123456789abcdef";
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,pixels,sizeof(pixels),digest,sizeof(digest))<0) { return false; }
    for (unsigned i=0;i<32;++i) { hash[2*i]=hex[digest[i]>>4]; hash[2*i+1]=hex[digest[i]&15u]; }
    hash[64]=0;
    int length=snprintf(path,sizeof(path),"%s/%s.rgba",directory,name);
    if (length<=0 || (size_t)length>=sizeof(path)) { return false; }
    FILE *file=NULL;
    if (fopen_s(&file,path,"wb") || !file) { return false; }
    bool written=fwrite(pixels,1,sizeof(pixels),file)==sizeof(pixels);
    if (fclose(file)) { written=false; }
    if (!written) { return false; }
    length=snprintf(path,sizeof(path),"%s/%s.json",directory,name);
    if (length<=0 || (size_t)length>=sizeof(path) || fopen_s(&file,path,"wb") || !file) { return false; }
    SYSTEMTIME now; GetSystemTime(&now);
    const char *screen=app.model.location.screen==APP_HOME ? "home" : grid ? "library-grid" : "library-list";
    written=fprintf(file,"{\"width\":1100,\"height\":900,\"logical_width\":1100,"
        "\"logical_height\":900,\"dpi\":96,\"scale\":1,\"zoom_percent\":100,"
        "\"screen\":\"%s\",\"renderer\":\"DX11 WARP\",\"theme\":\"dark\","
        "\"font\":\"Segoe UI / DirectWrite fallback\",\"language\":\"service-provided labels\","
        "\"source\":\"native client rendering retained live read-only service page\","
        "\"reference\":false,\"fixture\":false,\"resampled\":false,\"local_only\":true,"
        "\"artwork\":\"production bounded HTTPS/decode/cache; exact supplied sources\","
        "\"settled\":%s,\"page_rows\":%u,\"page_partial\":%s,\"visible_artwork\":%u,\"ready_artwork\":%u,"
        "\"failed_artwork\":%u,\"pending_artwork\":%u,\"missing_artwork\":%u,"
        "\"pixels_sha256\":\"%s\",\"captured_utc\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\"}\n",
        screen,complete ? "true" : "false",app.model.page->count,app.model.page->partial ? "true" : "false",
        image.visible,image.ready,image.failed,image.pending,image.missing,
        hash,(unsigned)now.wYear,(unsigned)now.wMonth,(unsigned)now.wDay,
        (unsigned)now.wHour,(unsigned)now.wMinute,(unsigned)now.wSecond)>0;
    if (fclose(file)) { written=false; }
    if (written) {
        printf("Native live capture %s: settled %u; visible %u; ready %u; failed %u; pending %u; missing %u; pixels SHA256 %s.\n",
            name,(unsigned)complete,image.visible,image.ready,image.failed,image.pending,image.missing,hash);
    }
    return written;
}
bool live_capture_shutdown(DWORD timeout_ms)
{
    if (!owner) { return true; }
    if (owner!=GetCurrentThreadId()) { return false; }
    bool stopped=app_artwork_close(&artwork,timeout_ms);
    /* Worker requests own their copied URLs. Retire the page even if an OS
     * request cannot stop in time, while preserving every callback dependency. */
    if (fonts_ready) { font_cache_destroy(&fonts); fonts_ready=false; }
    app.fonts=NULL; app.services.artwork=NULL;
    if (app.alive) { app_dispose(&app); }
    if (!stopped) { return false; }
    if (renderer_ready) { ui_renderer_destroy(&renderer); renderer_ready=false; }
    if (events) {
        if (event_queue_close(events)!=CORE_OK) { return false; }
        Event batch[16]; size_t count;
        CoreResult drained=event_drain(events,batch,16,&count);
        if ((drained!=CORE_OK && drained!=CORE_CLOSED) || event_queue_destroy(events)!=CORE_OK) { return false; }
        events=NULL;
    }
    if (core_ready) { arena_dispose(&core); core_ready=false; }
    bool balanced=(!metrics_ready || metrics_balanced(&metrics)) &&
        !font_stats(&fonts).com_objects && !ui_renderer_stats(&renderer).resources;
    metrics_ready=false; owner=0;
    SecureZeroMemory(pixels,sizeof(pixels));
    SecureZeroMemory(artwork.staging,sizeof(artwork.staging));
    return balanced;
}
bool live_capture_page(const CollectionPage *page,AppRequest request,CollectionResult result,
    const char *notice,const char *directory,const char *name,bool grid)
{
    if (owner || !page || !directory || !name_valid(name) || !request.generation ||
        request.generation==UINT64_MAX || page->json.generation!=request.generation ||
        page->kind!=request.kind || page->offset!=request.cursor ||
        (request.screen!=APP_HOME && request.screen!=APP_LIBRARY) ||
        (request.screen==APP_HOME && request.kind!=COLLECTION_HOME) ||
        (request.screen==APP_LIBRARY && request.kind!=COLLECTION_LIBRARY) ||
        (result!=COLLECTION_OK && result!=COLLECTION_PARTIAL && result!=COLLECTION_BOUNDARY)) { return false; }
    if (!CreateDirectoryA(directory,NULL) && GetLastError()!=ERROR_ALREADY_EXISTS) { return false; }
    DWORD attributes=GetFileAttributesA(directory);
    if (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY)) { return false; }
    owner=GetCurrentThreadId();
    ULONGLONG deadline=GetTickCount64()+CAPTURE_SETTLE_MS;
    bool ok=false,complete=false;
    metrics_ready=metrics_init(&metrics);
    if (!metrics_ready || !arena_init(&core,core_memory,sizeof(core_memory))) { goto done; }
    core_ready=true;
    if (!metrics_attach_arena(&metrics,&core,&core_metrics) ||
        event_queue_create(&core,16,&events)!=CORE_OK) { goto done; }
    UiRendererConfig config={.width=CAPTURE_WIDTH,.height=CAPTURE_HEIGHT,.warp=true,.debug=true};
    if (ui_renderer_create(&renderer,&config)!=UI_RENDER_OK) { goto done; }
    renderer_ready=true;
    memset(&fonts,0,sizeof(fonts));
    if (font_cache_create(&fonts,wake,events)!=FONT_OK) { goto done; }
    fonts_ready=true;
    ui_scheduler_init(&scheduler);
    AppServices services={.wake=wake,.context=events};
    if (app_init(&app,&services,&scheduler,clay,sizeof(clay),scratch,sizeof(scratch))!=APP_OK) { goto done; }
    app.model.generation=request.generation; app.model.request=request; app.model.serial=request.serial;
    app.model.location=(AppLocation){.screen=request.screen,.grid=grid};
    app.model.logged_in=app.model.online=app.model.pending=true;
    if (!page_retain((JsonPage *)&page->json)) { goto done; }
    if (app_deliver(&app,request,result,page)!=APP_OK) { goto done; } /* Consumes retained ref on every result. */
    if (notice && notice[0]) {
        size_t bytes=0;
        while (bytes<APP_NOTICE_BYTES && notice[bytes]) { ++bytes; }
        if (bytes==APP_NOTICE_BYTES ||
            app_set_notice(&app,APP_WARNING,(Clay_String){false,(int32_t)bytes,notice})!=APP_OK) { goto done; }
    }
    text=(FontMeasureContext){.cache=&fonts,.config={.dpi=96,.weight=400,.max_width=260,.ellipsis=true}};
    app_bind_fonts(&app,&text);
    if (!settle(deadline,&complete) || !complete) { goto done; }
    const float clear[4]={0,0,0,1};
    if (ui_renderer_submit(&renderer,&frame,clear)!=UI_RENDER_OK) { goto done; }
    /* As in the normal host, stage artwork only after useful content renders. */
    if (!app_artwork_open(&artwork,&core,&renderer,events,&metrics)) { goto done; }
    app.services.artwork=app_artwork_service(&artwork);
    if (!app.services.artwork || artwork_navigate(app.services.artwork,request.generation)!=ART_OK ||
        !settle(deadline,&complete)) { goto done; }
    if (ui_renderer_submit(&renderer,&frame,clear)!=UI_RENDER_OK ||
        ui_renderer_readback(&renderer,pixels,CAPTURE_WIDTH*4u,sizeof(pixels))!=UI_RENDER_OK) { goto done; }
    ok=save(directory,name,grid,complete,counts()) && complete;
done:
    {
        bool balanced=live_capture_shutdown(5000);
        printf("Native live capture cleanup: balanced %u.\n",(unsigned)balanced);
        return ok && balanced;
    }
}
