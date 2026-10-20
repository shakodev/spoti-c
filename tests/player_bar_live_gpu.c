/* Explicit account artwork + physical DX11 adapter check. No WARP fallback. */
#include "ui/app.h"
#include <stdio.h>

static UiRenderer renderer;
static UiRenderFrame frame;
static FontCache fonts;
static FontMeasureContext text;
static Arena arena;
static Worker *worker;
static Artwork *art;
static _Alignas(16) unsigned char storage[65536], workspace[ART_DECODE_BYTES];
static unsigned char upload[ART_UPLOAD_BYTES], core_memory[MEMORY_BUDGET_CORE];
static unsigned char pixels[1100u*720u*4u];
static bool font_ready;

bool player_bar_live_gpu_close(App *app)
{
    bool ok=true; app->fonts=NULL; app->services.artwork=NULL;
    if (art) {
        ArtResult r;
        while ((r=artwork_stop(art))==ART_PENDING) { (void)WaitForSingleObject(artwork_wait_handle(art),5000); }
        ok=r==ART_OK; art=NULL;
    }
    if (worker) { ok=worker_stop(worker)==CORE_OK && ok; ok=worker_join(worker,5000)==CORE_OK && ok; worker=NULL; }
    if (arena.base) { arena_dispose(&arena); }
    if (font_ready) { font_cache_destroy(&fonts); font_ready=false; }
    ui_renderer_destroy(&renderer); return ok;
}
bool player_bar_live_gpu_open(App *app)
{
    if (!arena_init(&arena,core_memory,sizeof(core_memory)) ||
        worker_start(&arena,4,(CoreClock){0},&worker)!=CORE_OK) { return false; }
    UiRendererConfig rc={.width=1100,.height=720};
    if (ui_renderer_create(&renderer,&rc)!=UI_RENDER_OK || ui_renderer_stats(&renderer).warp) { return false; }
    if (font_cache_create(&fonts,NULL,NULL)!=FONT_OK) { return false; } font_ready=true;
    text=(FontMeasureContext){.cache=&fonts,.config={.dpi=96,.weight=400,.max_width=260,.ellipsis=true}};
    ArtworkConfig ac={.worker=worker,.fetch=artwork_http_fetch,.gpu=artwork_renderer_gpu(&renderer),
        .decode_workspace=workspace,.upload=upload};
    if (artwork_init(storage,sizeof(storage),&ac,&art)!=ART_OK) { return false; }
    app->services.artwork=art; app_bind_fonts(app,&text);
    return artwork_navigate(art,app->model.generation)==ART_OK;
}
bool player_bar_live_gpu_capture(App *app,const char *name,uint32_t width)
{
    if (!art || !name || width>1100 || ui_renderer_resize(&renderer,width,720)!=UI_RENDER_OK) { return false; }
    uint64_t until=GetTickCount64()+15000; bool ready=false;
    do {
        (void)app_update(app,GetTickCount64());
        if (font_prepare_pending(&fonts)!=FONT_OK || font_upload_renderer(&fonts,&renderer)!=FONT_OK) { return false; }
        Clay_RenderCommandArray commands=app_layout(app,(float)width,720,96,(UiWidgetInput){.x=-10,.y=-10});
        UiClayBridge bridge={.custom=app_render_custom,.context=app}; const float clear[4]={0,0,0,1};
        if (app->widgets.error!=UI_WIDGET_OK || ui_frame_begin(&frame,width,720,1)!=UI_RENDER_OK ||
            ui_frame_clay(&frame,commands,&bridge)!=UI_RENDER_OK || ui_renderer_submit(&renderer,&frame,clear)!=UI_RENDER_OK) { return false; }
        ArtworkHandle handle; UiTexture texture;
        ready=artwork_lookup(art,app->now.cover.id,&handle) && artwork_resolve(art,handle,&texture) && !font_stats(&fonts).pending;
        if (!ready) { (void)WaitForSingleObject(artwork_wait_handle(art),20); }
    } while (!ready && GetTickCount64()<until);
    if (!ready || ui_renderer_readback(&renderer,pixels,(size_t)width*4,(size_t)width*720*4)!=UI_RENDER_OK) { return false; }
    Clay_BoundingBox box=Clay_GetElementData(CLAY_ID("PlayerArtwork")).boundingBox;
    if (box.width<1 || box.height<1 || box.x<0 || box.y<0 || box.x+box.width>width || box.y+box.height>720) { return false; }
    /* A resolved texture must actually replace the uniform placeholder pixels. */
    unsigned different=0; size_t first=((size_t)box.y*width+(size_t)box.x)*4;
    for (unsigned y=(unsigned)box.y;y<(unsigned)(box.y+box.height);++y) {
        for (unsigned x=(unsigned)box.x;x<(unsigned)(box.x+box.width);++x) {
            const unsigned char *p=pixels+((size_t)y*width+x)*4;
            different+=p[0]!=pixels[first] || p[1]!=pixels[first+1] || p[2]!=pixels[first+2];
        }
    }
    if (different<100) { return false; }
    char path[120]; if (sprintf_s(path,sizeof(path),"player-bar-live-%s-%ux720.rgba",name,width)<0) { return false; }
    FILE *file=NULL; if (fopen_s(&file,path,"wb") || !file) { return false; }
    size_t bytes=(size_t)width*720*4;
    bool ok=fwrite(pixels,1,bytes,file)==bytes; ok=fclose(file)==0 && ok;
    printf("Hardware DX11 live artwork: %s uploads=%llu varied_pixels=%u\n",path,
        (unsigned long long)artwork_stats(art).uploaded,different); return ok;
}
