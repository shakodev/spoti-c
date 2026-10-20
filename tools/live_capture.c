#include "live_capture.h"
#include "ui/app_artwork_host.h"
#include <bcrypt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CAPTURE_MAX_PIXELS (3600u*1800u*4u)
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
static unsigned char *pixels;
static size_t pixel_bytes;
static LiveCaptureViewport viewport;
static uint32_t physical_width,physical_height;
static DWORD owner;
static bool renderer_ready,fonts_ready,core_ready,metrics_ready;

typedef struct CaptureCounts {
    unsigned visible,ready,failed,pending,missing;
    unsigned sidebar_visible,sidebar_ready,sidebar_failed,sidebar_pending,sidebar_missing;
} CaptureCounts;
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
    out.sidebar_visible=app.sidebar.visible_count;
    for (unsigned i=0;i<app.sidebar.cover_count;++i) {
        ArtResult result=app.sidebar.covers[i].result;
        if (result==ART_OK) { ++out.sidebar_ready; }
        else if (result==ART_PENDING || result==ART_FULL) { ++out.sidebar_pending; }
        else { ++out.sidebar_failed; }
    }
    out.sidebar_missing=out.sidebar_visible>app.sidebar.cover_count ? out.sidebar_visible-app.sidebar.cover_count : 0;
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
    Clay_RenderCommandArray commands=app_layout(&app,(float)physical_width,(float)physical_height,(float)viewport.dpi,
        (UiWidgetInput){.x=-1,.y=-1});
    if (!commands.length || app.widgets.error!=UI_WIDGET_OK ||
        ui_frame_begin(&frame,physical_width,physical_height,(float)viewport.dpi/96)!=UI_RENDER_OK) { return false; }
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
        if (pass>2 && !font_stats(&fonts).pending && !art.active && !art.pending && !images.pending && !images.sidebar_pending) {
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
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,pixels,(ULONG)pixel_bytes,digest,sizeof(digest))<0) { return false; }
    for (unsigned i=0;i<32;++i) { hash[2*i]=hex[digest[i]>>4]; hash[2*i+1]=hex[digest[i]&15u]; }
    hash[64]=0;
    int length=snprintf(path,sizeof(path),"%s/%s.rgba",directory,name);
    if (length<=0 || (size_t)length>=sizeof(path)) { return false; }
    FILE *file=NULL;
    if (fopen_s(&file,path,"wb") || !file) { return false; }
    bool written=fwrite(pixels,1,pixel_bytes,file)==pixel_bytes;
    if (fclose(file)) { written=false; }
    if (!written) { return false; }
    length=snprintf(path,sizeof(path),"%s/%s.json",directory,name);
    if (length<=0 || (size_t)length>=sizeof(path) || fopen_s(&file,path,"wb") || !file) { return false; }
    SYSTEMTIME now; GetSystemTime(&now);
    Clay_BoundingBox header=Clay_GetElementData(CLAY_ID("AppTitle")).boundingBox;
    Clay_BoundingBox sidebar=Clay_GetElementData(CLAY_ID("AppSidebar")).boundingBox;
    Clay_BoundingBox main=Clay_GetElementData(CLAY_ID("AppMain")).boundingBox;
    Clay_BoundingBox bar=Clay_GetElementData(CLAY_ID("AppNowPlaying")).boundingBox;
    const char *screen=app.model.location.screen==APP_HOME ? "home" : grid ? "library-grid" : "library-list";
    written=fprintf(file,"{\"width\":%u,\"height\":%u,\"logical_width\":%u,"
        "\"logical_height\":%u,\"dpi\":%u,\"scale\":%.2f,\"zoom_percent\":100,\"readback_bytes\":%zu,"
        "\"screen\":\"%s\",\"renderer\":\"DX11 WARP\",\"theme\":\"dark\","
        "\"font\":\"Segoe UI / DirectWrite fallback\",\"language\":\"service-provided labels\","
        "\"geometry_dip\":{\"header_height\":%g,\"panel_y\":%g,\"sidebar_width\":%g,"
        "\"panel_gap\":%g,\"player_y\":%g,\"player_height\":%g},"
        "\"source\":\"native client rendering retained live read-only service page\","
        "\"reference\":false,\"fixture\":false,\"resampled\":false,\"local_only\":true,"
        "\"artwork\":\"production bounded HTTPS/decode/cache; exact supplied sources\","
        "\"settled\":%s,\"page_rows\":%u,\"page_partial\":%s,\"visible_artwork\":%u,\"ready_artwork\":%u,"
        "\"failed_artwork\":%u,\"pending_artwork\":%u,\"missing_artwork\":%u,"
        "\"sidebar_source\":\"%s\",\"sidebar_rows\":%u,\"sidebar_page_limit\":16,"
        "\"sidebar_visible_artwork\":%u,\"sidebar_ready_artwork\":%u,\"sidebar_failed_artwork\":%u,"
        "\"sidebar_pending_artwork\":%u,\"sidebar_missing_artwork\":%u,"
        "\"pixels_sha256\":\"%s\",\"captured_utc\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\"}\n",
        physical_width,physical_height,viewport.width,viewport.height,viewport.dpi,(double)viewport.dpi/96,pixel_bytes,
        screen,(double)header.height,(double)main.y,(double)sidebar.width,
        (double)(main.x-sidebar.x-sidebar.width),(double)bar.y,(double)bar.height,
        complete ? "true" : "false",app.model.page->count,app.model.page->partial ? "true" : "false",
        image.visible,image.ready,image.failed,image.pending,image.missing,
        app.sidebar.page ? "retained live rootlist playlists" : "unavailable",app.sidebar.page ? app.sidebar.page->count : 0,
        image.sidebar_visible,image.sidebar_ready,image.sidebar_failed,image.sidebar_pending,image.sidebar_missing,
        hash,(unsigned)now.wYear,(unsigned)now.wMonth,(unsigned)now.wDay,
        (unsigned)now.wHour,(unsigned)now.wMinute,(unsigned)now.wSecond)>0;
    if (fclose(file)) { written=false; }
    if (written) {
        printf("Native live capture %s: %u x %u pixels; %u DPI; settled %u; visible %u; ready %u; failed %u; pending %u; missing %u; sidebar ready %u/%u; pixels SHA256 %s.\n",
            name,physical_width,physical_height,viewport.dpi,(unsigned)complete,image.visible,image.ready,image.failed,image.pending,image.missing,
            image.sidebar_ready,image.sidebar_visible,hash);
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
    if (pixels) {
        SecureZeroMemory(pixels,pixel_bytes);
        if (!VirtualFree(pixels,0,MEM_RELEASE)) { return false; }
        pixels=NULL; pixel_bytes=0;
    }
    metrics_ready=false; owner=0;
    SecureZeroMemory(artwork.staging,sizeof(artwork.staging));
    return balanced;
}
static bool retain_sidebar(const AppRootlistPage *page)
{
    if (!page || page->count>APP_ROOTLIST_ROWS) { return false; }
    atomic_uint *references=&((AppRootlistPage *)page)->references;
    unsigned count=atomic_load_explicit(references,memory_order_acquire);
    while (count && count<UINT_MAX-1) {
        if (atomic_compare_exchange_weak_explicit(references,&count,count+1,memory_order_acq_rel,memory_order_acquire)) { return true; }
    }
    return false;
}
bool live_capture_page_at(const CollectionPage *page,AppRequest request,CollectionResult result,
    const char *notice,const char *directory,const char *name,bool grid,LiveCaptureViewport size,const AppRootlistPage *sidebar)
{
    if (owner || !page || !directory || !name_valid(name) || !request.generation || request.sidebar ||
        size.width<400 || size.width>1920 || size.height<320 || size.height>1100 || size.dpi<96 || size.dpi>192 ||
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
    viewport=size; physical_width=(uint32_t)MulDiv((int)size.width,(int)size.dpi,96);
    physical_height=(uint32_t)MulDiv((int)size.height,(int)size.dpi,96);
    pixel_bytes=(size_t)physical_width*physical_height*4u;
    ULONGLONG deadline=GetTickCount64()+CAPTURE_SETTLE_MS;
    bool ok=false,complete=false;
    if (!pixel_bytes || pixel_bytes>CAPTURE_MAX_PIXELS) { goto done; }
    pixels=VirtualAlloc(NULL,pixel_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if (!pixels) { goto done; }
    metrics_ready=metrics_init(&metrics);
    if (!metrics_ready || !arena_init(&core,core_memory,sizeof(core_memory))) { goto done; }
    core_ready=true;
    if (!metrics_attach_arena(&metrics,&core,&core_metrics) ||
        event_queue_create(&core,16,&events)!=CORE_OK) { goto done; }
    UiRendererConfig config={.width=physical_width,.height=physical_height,.warp=true,.debug=true};
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
    if (sidebar) {
        if (!retain_sidebar(sidebar)) { goto done; }
        app.sidebar.page=sidebar; /* app_dispose consumes only this retained reference. */
    }
    if (notice && notice[0]) {
        size_t bytes=0;
        while (bytes<APP_NOTICE_BYTES && notice[bytes]) { ++bytes; }
        if (bytes==APP_NOTICE_BYTES ||
            app_set_notice(&app,APP_INFORMATION,(Clay_String){false,(int32_t)bytes,notice})!=APP_OK) { goto done; }
    }
    text=(FontMeasureContext){.cache=&fonts,.config={.dpi=(uint16_t)size.dpi,.weight=400,.max_width=260,.ellipsis=true}};
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
        ui_renderer_readback(&renderer,pixels,physical_width*4u,pixel_bytes)!=UI_RENDER_OK) { goto done; }
    ok=save(directory,name,grid,complete,counts()) && complete;
done:
    {
        bool balanced=live_capture_shutdown(5000);
        printf("Native live capture cleanup: balanced %u.\n",(unsigned)balanced);
        return ok && balanced;
    }
}
bool live_capture_page(const CollectionPage *page,AppRequest request,CollectionResult result,
    const char *notice,const char *directory,const char *name,bool grid)
{
    return live_capture_page_at(page,request,result,notice,directory,name,grid,(LiveCaptureViewport){1100,900,96},NULL);
}
bool live_capture_matrix(const CollectionPage *page,AppRequest request,CollectionResult result,
    const char *notice,const char *directory,const char *name,const AppRootlistPage *sidebar)
{
    if (!name_valid(name) || !page || (request.screen!=APP_HOME && request.screen!=APP_LIBRARY)) { return false; }
    static const uint32_t widths[]={400,1100,1800},dpis[]={96,120,144,192};
    bool ok=true; unsigned views=request.screen==APP_LIBRARY ? 2 : 1;
    for (unsigned view=0;view<views;++view) {
        for (unsigned w=0;w<3;++w) {
            for (unsigned d=0;d<4;++d) {
                char identity[64]; int n=snprintf(identity,sizeof(identity),"%s%s-%u-%u",name,
                    request.screen==APP_LIBRARY ? view ? "-grid" : "-list" : "",widths[w],dpis[d]);
                if (n<=0 || (size_t)n>=sizeof(identity)) { return false; }
                bool captured=live_capture_page_at(page,request,result,notice,directory,identity,view!=0,
                    (LiveCaptureViewport){widths[w],900,dpis[d]},sidebar);
                ok=captured && ok;
                if (owner) { return false; } /* Preserve dependencies after incomplete cleanup. */
            }
        }
    }
    return ok;
}
bool live_capture_reference(const CollectionPage *page,AppRequest request,CollectionResult result,
    const char *notice,const char *directory,const char *name,const AppRootlistPage *sidebar)
{
    if (!name_valid(name) || !page || (request.screen!=APP_HOME && request.screen!=APP_LIBRARY)) { return false; }
    static const LiveCaptureViewport sizes[]={
        {1100,900,96},{1600,900,96},{1920,1033,96},
        {800,816,120},{1100,816,120},{1536,817,120},
        {800,672,144},{983,672,144},{1280,673,144}};
    static const char *const widths[]={"narrow","default","wide"};
    unsigned views=request.screen==APP_LIBRARY ? 2 : 1; bool ok=true;
    for (unsigned view=0;view<views;++view) {
        for (unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
            char identity[64]; int n=snprintf(identity,sizeof(identity),"%s%s-reference-%s-%u",name,
                request.screen==APP_LIBRARY ? view ? "-grid" : "-list" : "",widths[i%3],sizes[i].dpi);
            if (n<=0 || (size_t)n>=sizeof(identity)) { return false; }
            LiveCaptureViewport size=sizes[i];
            /* The separately captured 150% default grid stopped at 976 DIP;
             * Home/list stopped at 983. Preserve observed sizes exactly. */
            if (view && i==7) { size.width=976; }
            ok=live_capture_page_at(page,request,result,notice,directory,identity,view!=0,size,sidebar) && ok;
            if (owner) { return false; }
        }
    }
    return ok;
}
