#include "app_shell_support.h"
#include "artwork_fixtures.h"
#include "support/ui_heap_watch.h"
#include <bcrypt.h>
#include <wchar.h>

/* Native DX11 output from synthetic pages and injected PNG bytes. This fixture
 * neither contacts Spotify nor claims to capture a signed-in reference. */
static FontCache fonts;
static FontMeasureContext text;
static UiRenderer renderer;
static UiRenderFrame render_frame;
static _Alignas(16) unsigned char core_memory[MEMORY_BUDGET_CORE];
static _Alignas(16) unsigned char art_storage[65536], workspace[ART_DECODE_BYTES];
static unsigned char upload[ART_UPLOAD_BYTES];
static Arena core;
static Worker *worker;
static Artwork *art;
static DWORD owner;
static _Atomic unsigned fetched, broken;

static ArtResult fetch(void *context,const wchar_t *url,void *buffer,size_t capacity,
    size_t *bytes,const CancelToken *cancel)
{
    (void)context;
    CHECK(GetCurrentThreadId()!=owner);
    CHECK(!wcsncmp(url,L"https://example.test/shell-art-",31));
    if (worker_cancelled(cancel)) { *bytes=0; return ART_CANCELLED; }
    CHECK(capacity>=sizeof(art_fixture_png));
    memcpy(buffer,art_fixture_png,sizeof(art_fixture_png));
    if (!wcscmp(url,L"https://example.test/shell-art-6.png")) {
        ((unsigned char *)buffer)[sizeof(art_fixture_png)-1]^=1;
        atomic_fetch_add(&broken,1);
    }
    *bytes=sizeof(art_fixture_png); atomic_fetch_add(&fetched,1); return ART_OK;
}
static void open_artwork(void)
{
    owner=GetCurrentThreadId(); atomic_store(&fetched,0); atomic_store(&broken,0);
    CHECK(arena_init(&core,core_memory,sizeof(core_memory)));
    CHECK(worker_start(&core,ART_REQUESTS,(CoreClock){0},&worker)==CORE_OK);
    ArtworkConfig config={.worker=worker,.fetch=fetch,.gpu=artwork_renderer_gpu(&renderer),
        .decode_workspace=workspace,.upload=upload};
    CHECK(artwork_size()<=sizeof(art_storage));
    CHECK(artwork_init(art_storage,sizeof(art_storage),&config,&art)==ART_OK);
    CHECK(artwork_navigate(art,app.model.generation)==ART_OK);
    app.services.artwork=art;
}
static void close_artwork(void)
{
    app.services.artwork=NULL;
    ArtResult result=artwork_stop(art);
    CHECK(result==ART_OK || result==ART_PENDING);
    CHECK(worker_stop(worker)==CORE_OK && worker_join(worker,5000)==CORE_OK);
    if (result==ART_PENDING) { CHECK(artwork_stop(art)==ART_OK); }
    art=NULL; worker=NULL; arena_dispose(&core);
}
static void layout(unsigned width,unsigned height,unsigned dpi)
{
    Clay_RenderCommandArray commands=shell_layout((float)width,(float)height,(float)dpi);
    CHECK(ui_frame_begin(&render_frame,width,height,(float)dpi/96)==UI_RENDER_OK);
    UiClayBridge bridge={.custom=app_render_custom,.context=&app};
    CHECK(ui_frame_clay(&render_frame,commands,&bridge)==UI_RENDER_OK);
}
static void save(const char *screen,const char *size,unsigned width,unsigned height,
    unsigned logical_width,unsigned dpi,const unsigned char *rgba,unsigned ready,unsigned failed)
{
    char path[192];
    unsigned char digest[32]; char hash[65]; const char hex[]="0123456789abcdef";
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)rgba,width*height*4u,digest,sizeof(digest))>=0);
    for (unsigned i=0;i<32;++i) { hash[i*2]=hex[digest[i]>>4]; hash[i*2+1]=hex[digest[i]&15u]; } hash[64]=0;
    SYSTEMTIME now; GetSystemTime(&now);
    CHECK(snprintf(path,sizeof(path),"shell-captures/%s-%s-%u.rgba",screen,size,dpi)>0);
    FILE *file=NULL; CHECK(!fopen_s(&file,path,"wb") && file);
    CHECK(fwrite(rgba,4,(size_t)width*height,file)==(size_t)width*height && !fclose(file));
    CHECK(snprintf(path,sizeof(path),"shell-captures/%s-%s-%u.json",screen,size,dpi)>0);
    CHECK(!fopen_s(&file,path,"wb") && file);
    CHECK(fprintf(file,"{\"width\":%u,\"height\":%u,\"logical_width\":%u,"
        "\"logical_height\":900,\"dpi\":%u,\"scale\":%.2f,\"screen\":\"%s\","
        "\"size\":\"%s\",\"renderer\":\"DX11 WARP\",\"theme\":\"dark\","
        "\"language\":\"English with synthetic Hebrew names\",\"font\":\"Segoe UI / DirectWrite fallback\","
        "\"source\":\"native synthetic fixture; not a Spotify reference screenshot\","
        "\"account\":\"none\",\"network\":false,\"reference\":false,\"resampled\":false,"
        "\"zoom_percent\":100,\"pixels_sha256\":\"%s\","
        "\"captured_utc\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\","
        "\"artwork\":\"injected 4x2 PNG; one corrupt source and missing metadata placeholders\","
        "\"ready_artwork\":%u,\"failed_artwork\":%u}\n",
        width,height,logical_width,dpi,(double)dpi/96,screen,size,hash,(unsigned)now.wYear,
        (unsigned)now.wMonth,(unsigned)now.wDay,(unsigned)now.wHour,(unsigned)now.wMinute,
        (unsigned)now.wSecond,ready,failed)>0);
    CHECK(!fclose(file));
    printf("native fixture capture: %s/%s %u DIP at %u DPI -> %ux%u, artwork ready=%u failed=%u\n",
        screen,size,logical_width,dpi,width,height,ready,failed);
}
static void capture(const char *screen,const char *size,unsigned logical_width,unsigned dpi)
{
    unsigned width=logical_width*dpi/96,height=900u*dpi/96;
    CHECK(font_cache_reset(&fonts)==FONT_OK);
    text.config.dpi=(uint16_t)dpi; app_bind_fonts(&app,&text);
    CHECK(ui_renderer_resize(&renderer,width,height)==UI_RENDER_OK);
    app.model.location.scroll=0;
    for (unsigned i=0;i<UI_WIDGET_LISTS;++i) { app.widgets.lists[i].scroll=0; }
    bool settled=false;
    for (unsigned n=0;n<160;++n) {
        (void)app_update(&app,n);
        CHECK(font_prepare_pending(&fonts)==FONT_OK && font_upload_renderer(&fonts,&renderer)==FONT_OK);
        layout(width,height,dpi);
        ArtworkStats stats=artwork_stats(art);
        if (n>3 && !font_stats(&fonts).pending && !stats.pending && !stats.active) { settled=true; break; }
        if (stats.active) { (void)WaitForSingleObject(artwork_wait_handle(art),1000); }
    }
    CHECK(settled && app.browse.visible_count>0 && app.image_count>0);
    unsigned ready=0,failed=0;
    for (unsigned i=0;i<app.image_count;++i) {
        if (app.images[i].result==ART_OK) { ++ready; }
        else if (app.images[i].result==ART_DECODE_ERROR) { ++failed; }
    }
    CHECK(ready>0);
    FontStats before=font_stats(&fonts); ArtworkStats art_before=artwork_stats(art);
    unsigned fetch_before=atomic_load(&fetched);
    ui_heap_begin();
    for (unsigned i=0;i<100;++i) { layout(width,height,dpi); }
    ui_heap_end();
    CHECK(!font_stats(&fonts).pending && font_stats(&fonts).prepared==before.prepared);
    CHECK(artwork_stats(art).submitted==art_before.submitted && atomic_load(&fetched)==fetch_before);
    const float clear[4]={0,0,0,1};
    CHECK(ui_renderer_submit(&renderer,&render_frame,clear)==UI_RENDER_OK);
    size_t bytes=(size_t)width*height*4;
    unsigned char *rgba=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(rgba);
    CHECK(ui_renderer_readback(&renderer,rgba,(size_t)width*4,bytes)==UI_RENDER_OK);
    unsigned artwork_pixels=0,ink=0;
    for (size_t i=0;i<(size_t)width*height;++i) {
        const unsigned char *pixel=rgba+i*4;
        CHECK(pixel[3]==255);
        if (pixel[0]==32 && pixel[1]==96 && pixel[2]==160) { ++artwork_pixels; }
        if (pixel[0]>180 && pixel[1]>180 && pixel[2]>180) { ++ink; }
    }
    save(screen,size,width,height,logical_width,dpi,rgba,ready,failed);
    fprintf(stderr,"pixel coverage %s/%s/%u: artwork=%u ink=%u\n",screen,size,dpi,artwork_pixels,ink);
    CHECK(artwork_pixels>500 && ink>500);
    CHECK(VirtualFree(rgba,0,MEM_RELEASE));
}
int shell_visual(void)
{
    if (!CreateDirectoryW(L"shell-captures",NULL)) { CHECK(GetLastError()==ERROR_ALREADY_EXISTS); }
    UiRendererConfig config={.width=1100,.height=900,.warp=true,.debug=true};
    UiRenderResult result=ui_renderer_create(&renderer,&config);
    if (result==UI_RENDER_UNSUPPORTED) { puts("SKIP: shell WARP unavailable"); return 77; }
    CHECK(result==UI_RENDER_OK && font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    text=(FontMeasureContext){.cache=&fonts,.config={.dpi=96,.weight=400,.max_width=260,.ellipsis=true}};
    app_bind_fonts(&app,&text); open_artwork();
    app.model.detail=false;
    shell_action(APP_RETRY); shell_dense_page(true);
    const unsigned widths[]={400,1100,1800},dpis[]={96,120,144,192};
    const char *sizes[]={"narrow","default","wide"},*screens[]={"home","library-list","library-grid"};
    for (unsigned screen=0;screen<3;++screen) {
        if (screen==1) { shell_action(APP_GO_LIBRARY); shell_dense_page(false); }
        if (screen==2) { shell_action(APP_LIBRARY_GRID); CHECK(app.model.location.grid); }
        for (unsigned dpi=0;dpi<4;++dpi) {
            for (unsigned size=0;size<3;++size) { capture(screens[screen],sizes[size],widths[size],dpis[dpi]); }
        }
    }
    CHECK(atomic_load(&fetched)>0 && atomic_load(&broken)>0);
    close_artwork(); app.fonts=NULL; font_cache_destroy(&fonts);
    CHECK(font_stats(&fonts).com_objects==0 && ui_renderer_stats(&renderer).textures==0);
    CHECK(ui_renderer_debug_report(&renderer) && ui_renderer_stats(&renderer).resources==0);
    puts("36 native shell fixtures: Home/Library list/grid, three sizes, four DPI scales, injected artwork, no network");
    return 0;
}
