#include "ui/renderer.h"
#include "support/test_check.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <string.h>

static UiRenderer renderer;
static UiRenderFrame frame;
static unsigned char pixels[256 * 256 * 4];
static uint32_t width;
static float scale;

static QuadInstance shape(float x, float y, float w, float h, float red, float green, float blue, float alpha)
{
    QuadInstance q = {.box = {x,y,w,h}, .color = {red,green,blue,alpha}, .uv = {0,0,1,1}};
    return q;
}
static void begin(void) { CHECK(ui_frame_begin(&frame, width, width, scale) == UI_RENDER_OK); }
static void render(void)
{
    const float clear[4] = {0};
    CHECK(ui_renderer_submit(&renderer, &frame, clear) == UI_RENDER_OK);
    CHECK(ui_renderer_readback(&renderer, pixels, width * 4, sizeof(pixels)) == UI_RENDER_OK);
}
static void pixel(unsigned x, unsigned y, int r, int g, int b, int a, int tolerance)
{
    unsigned px = (unsigned)((float)x * scale), py = (unsigned)((float)y * scale);
    const unsigned char *p = pixels + ((size_t)py * width + px) * 4;
    int expected[4] = {r,g,b,a};
    for (unsigned i = 0; i < 4; ++i) {
        if (abs((int)p[i] - expected[i]) > tolerance) {
            fprintf(stderr, "scale %.2f pixel %u,%u channel %u: got %u expected %d +/- %d\n",
                (double)scale, x, y, i, p[i], expected[i], tolerance);
            CHECK(false);
        }
    }
}

static void golden_shapes(unsigned pass)
{
    begin();
    QuadInstance q = shape(8,8,32,24,1,0,0,1);
    q.radii[0] = 8; q.radii[2] = 8;
    CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK); render();
    pixel(8,8,0,0,0,0,0); pixel(20,20,255,0,0,255,0);
    pixel(39,8,255,0,0,255,1); pixel(39,31,0,0,0,0,0);
    begin(); q = shape(8.25f,8,16,16,1,0,0,1);
    CHECK(ui_frame_quad(&frame,&q,0) == UI_RENDER_OK); render();
    const int edge[3] = {191,159,128};
    pixel(8,16,edge[pass],0,0,edge[pass],1);
    begin(); q = shape(8,8,32,24,0,1,0,1); q.effect[0] = UI_QUAD_BORDER;
    q.radii[0] = q.radii[1] = q.radii[2] = q.radii[3] = 4;
    q.border[0] = 2; q.border[1] = 4; q.border[2] = 6; q.border[3] = 8;
    CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK); render();
    pixel(20,20,0,0,0,0,0); pixel(8,20,0,255,0,255,1);
    pixel(20,9,0,255,0,255,1); pixel(36,20,0,255,0,255,1);
    pixel(20,29,0,255,0,255,1); pixel(8,8,0,0,0,0,1);
    begin(); q = shape(8.25f,8,16,16,1,0,0,1); q.effect[0] = UI_QUAD_BORDER;
    CHECK(ui_frame_quad(&frame,&q,0) == UI_RENDER_OK); render(); pixel(8,16,0,0,0,0,0);
    begin(); q = shape(20,20,16,16,0,0,1,1);
    q.effect[0] = UI_QUAD_SHADOW; q.effect[1] = 6;
    q.radii[0] = q.radii[1] = q.radii[2] = q.radii[3] = 4;
    CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK); render();
    const int shadow[3] = {44,39,36};
    pixel(13,28,0,0,0,0,0); pixel(16,28,0,0,shadow[pass],shadow[pass],2);
    pixel(28,28,0,0,255,255,1); pixel(42,28,0,0,0,0,0);
}

static void golden_textures(void)
{
    unsigned char green[4] = {0,255,0,128}, coverage[4] = {64,0,0,0};
    UiTexture image = 0, glyph = 0;
    CHECK(ui_renderer_texture(&renderer, 1,1,green,4,&image) == UI_RENDER_OK);
    CHECK(ui_renderer_texture(&renderer, 1,1,coverage,4,&glyph) == UI_RENDER_OK);
    begin(); QuadInstance q = shape(8,8,32,24,1,1,1,1); q.effect[0] = UI_QUAD_IMAGE;
    CHECK(ui_frame_quad(&frame, &q, image) == UI_RENDER_OK);
    q = shape(16,16,16,16,1,0,0,0.5f); CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK);
    render(); pixel(10,10,0,128,0,128,1); pixel(20,20,128,64,0,192,1);
    begin(); q = shape(16,16,16,16,1,0,0,0.5f); CHECK(ui_frame_quad(&frame,&q,0) == UI_RENDER_OK);
    q = shape(8,8,32,24,1,1,1,1); q.effect[0] = UI_QUAD_IMAGE;
    CHECK(ui_frame_quad(&frame,&q,image) == UI_RENDER_OK); render(); pixel(20,20,64,128,0,192,1);
    begin(); q = shape(8,8,32,24,0,0,1,0.5f); q.effect[0] = UI_QUAD_GLYPH;
    CHECK(ui_frame_quad(&frame,&q,glyph) == UI_RENDER_OK); render(); pixel(20,20,0,0,32,32,1);
    CHECK(ui_renderer_texture_release(&renderer,image) == UI_RENDER_OK);
    CHECK(ui_renderer_texture_release(&renderer,image) == UI_RENDER_INVALID);
    CHECK(ui_renderer_texture_release(&renderer,glyph) == UI_RENDER_OK);
    unsigned char checker[16] = {255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    CHECK(ui_renderer_texture(&renderer,2,2,checker,8,&image) == UI_RENDER_OK);
    begin(); q = shape(8,8,32,24,1,1,1,1); q.effect[0] = UI_QUAD_IMAGE;
    CHECK(ui_frame_quad(&frame,&q,image) == UI_RENDER_OK); render();
    pixel(9,9,255,0,0,255,0); pixel(39,9,0,255,0,255,0);
    pixel(9,31,0,0,255,255,0); pixel(39,31,255,255,255,255,0);
    begin(); q.uv[0] = q.uv[2] = 0.75f; q.uv[1] = q.uv[3] = 0.25f;
    CHECK(ui_frame_quad(&frame,&q,image) == UI_RENDER_OK); render(); pixel(20,20,0,255,0,255,0);
    CHECK(ui_renderer_texture_release(&renderer,image) == UI_RENDER_OK);
}

static void golden_scissors(void)
{
    begin(); QuadInstance q = shape(0,0,64,64,1,0,0,1);
    CHECK(ui_frame_clip_push(&frame,(UiRect){8,8,32,32},true,true) == UI_RENDER_OK);
    CHECK(ui_frame_clip_push(&frame,(UiRect){16,16,8,8},true,true) == UI_RENDER_OK);
    CHECK(ui_frame_quad(&frame,&q,0) == UI_RENDER_OK); CHECK(ui_frame_clip_pop(&frame) == UI_RENDER_OK);
    q.color[0] = 0; q.color[2] = 1; q.color[3] = 0.5f;
    CHECK(ui_frame_quad(&frame,&q,0) == UI_RENDER_OK); CHECK(ui_frame_clip_pop(&frame) == UI_RENDER_OK);
    q = shape(50,50,8,8,0,1,0,1); CHECK(ui_frame_quad(&frame,&q,0) == UI_RENDER_OK);
    render(); pixel(4,4,0,0,0,0,0); pixel(12,12,0,0,128,128,1);
    pixel(20,20,127,0,128,255,1); pixel(52,52,0,255,0,255,0);
}

static void limits_and_recovery(void)
{
    UiTexture textures[UI_MAX_TEXTURES], extra = 0; unsigned char white[4] = {255,255,255,255};
    for (unsigned i = 0; i < UI_MAX_TEXTURES; ++i) {
        CHECK(ui_renderer_texture(&renderer,1,1,white,4,&textures[i]) == UI_RENDER_OK);
    }
    CHECK(ui_renderer_texture(&renderer,1,1,white,4,&extra) == UI_RENDER_FULL && !extra);
    UiTexture stale = textures[0];
    ui_renderer_invalidate(&renderer);
    begin(); const float clear[4] = {0};
    CHECK(ui_renderer_submit(&renderer,&frame,clear) == UI_RENDER_DEVICE_LOST);
    CHECK(ui_renderer_recover(&renderer) == UI_RENDER_OK);
    CHECK(ui_renderer_stats(&renderer).textures == 0);
    CHECK(ui_renderer_texture_release(&renderer,stale) == UI_RENDER_INVALID);
    CHECK(ui_renderer_texture(&renderer,1,1,white,4,&extra) == UI_RENDER_OK && extra != stale);
    begin(); QuadInstance q = shape(0,0,20,20,1,1,1,1); q.effect[0] = UI_QUAD_IMAGE;
    CHECK(ui_frame_quad(&frame,&q,stale) == UI_RENDER_OK);
    uint64_t maps = ui_renderer_stats(&renderer).maps;
    CHECK(ui_renderer_submit(&renderer,&frame,clear) == UI_RENDER_INVALID);
    CHECK(ui_renderer_stats(&renderer).maps == maps);
    CHECK(ui_renderer_texture_release(&renderer,extra) == UI_RENDER_OK);
    UiRendererStats before = ui_renderer_stats(&renderer);
    for (unsigned i = 0; i < 20; ++i) {
        CHECK(ui_renderer_resize(&renderer,64,80) == UI_RENDER_OK);
        CHECK(ui_renderer_resize(&renderer,128,128) == UI_RENDER_OK);
    }
    CHECK(ui_renderer_resize(&renderer,0,0) == UI_RENDER_SUSPENDED);
    CHECK(ui_renderer_stats(&renderer).resources == before.resources);
    CHECK(ui_renderer_stats(&renderer).owned_gpu_bytes <= sizeof(QuadInstance) * UI_MAX_QUADS + 16 + 128 * 128 * 4);
}

int main(void)
{
    UiRendererConfig config = {.width = 128, .height = 128, .warp = true, .debug = true};
    UiRenderResult result = ui_renderer_create(&renderer,&config);
    if (result == UI_RENDER_UNSUPPORTED) {
        fprintf(stderr,"SKIP: WARP device unavailable (renderer result %d)\n",result); return 77;
    }
    CHECK(result == UI_RENDER_OK);
    printf("WARP debug layer: %s\n",ui_renderer_stats(&renderer).debug_layer ? "enabled" : "unavailable; using owned-resource ledger");
    const float scales[3] = {1,1.5f,2};
    for (unsigned i = 0; i < 3; ++i) {
        scale = scales[i]; width = (uint32_t)(128 * scale);
        CHECK(ui_renderer_resize(&renderer,width,width) == UI_RENDER_OK);
        golden_shapes(i); golden_textures(); golden_scissors();
    }
    limits_and_recovery();
    CHECK(ui_renderer_debug_report(&renderer));
    CHECK(ui_renderer_stats(&renderer).resources == 0);
    puts("WARP: golden RGBA samples at 96/144/192 DPI, textures, clips, resize and recovery passed");
    return 0;
}
