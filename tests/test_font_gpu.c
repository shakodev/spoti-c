#include "ui/font.h"
#include "support/test_check.h"
#include <stdio.h>
#include <string.h>

static FontCache cache;
static UiRenderer renderer;
static UiRenderFrame frame;
static unsigned char pixels[512 * 256 * 4];
static Clay_StringSlice slice(const char *s) { return (Clay_StringSlice){(int32_t)strlen(s),s,s}; }

static void visual(unsigned dpi)
{
    const char *lines[] = {"Hello office e\xcc\x81", "\xd8\xa7\xd9\x84\xd8\xb9\xd8\xb1\xd8\xa8\xd9\x8a\xd8\xa9 123",
        "\xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80", "A long name with a shaped ellipsis"};
    const float scale = (float)dpi / 96; unsigned width = (unsigned)(256 * scale), height = (unsigned)(128 * scale);
    CHECK(font_cache_reset(&cache) == FONT_OK);
    CHECK(ui_renderer_resize(&renderer,width,height) == UI_RENDER_OK);
    FontHandle handles[4]; FontMetrics metrics[4];
    for (unsigned i = 0; i < 4; ++i) {
        FontConfig config = {.size=16,.dpi=(uint16_t)dpi,.weight=400,.max_width=120,.ellipsis=true,.rtl=i == 1};
        CHECK(font_prepare_text(&cache,slice(lines[i]),&config,&handles[i]) == FONT_OK);
        CHECK(font_metrics(&cache,handles[i],&metrics[i]) == FONT_OK && metrics[i].width > 0);
    }
    CHECK(font_upload_renderer(&cache,&renderer) == FONT_OK);
    UiRendererStats stats = ui_renderer_stats(&renderer);
    CHECK(stats.textures == 1);
    CHECK(renderer.textures[(cache.texture & 255u)-1].bytes == 1024 * 1024);
    CHECK(renderer.textures[(cache.texture & 255u)-1].channels == 1);
    CHECK(ui_frame_begin(&frame,width,height,scale) == UI_RENDER_OK);
    const float white[4] = {1,1,1,1}, clear[4] = {0};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(font_render_text(&cache,handles[i],&frame,8,4 + 30 * (float)i,white) == FONT_OK);
    }
    CHECK(frame.clip_count == 1);
    CHECK(ui_renderer_submit(&renderer,&frame,clear) == UI_RENDER_OK);
    CHECK(ui_renderer_readback(&renderer,pixels,width * 4,sizeof(pixels)) == UI_RENDER_OK);
    unsigned ink[4] = {0}, gray = 0;
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            unsigned char *p = pixels + ((size_t)y * width + x) * 4;
            CHECK(p[0] == p[1] && p[1] == p[2] && p[2] == p[3]);
            if (p[3]) {
                CHECK((float)x >= 8 * scale && (float)x < 128 * scale);
                unsigned row = (unsigned)((float)y / scale) / 30;
                CHECK(row < 4); ++ink[row];
                if (p[3] > 8 && p[3] < 247) { ++gray; }
            }
        }
    }
    for (unsigned i = 0; i < 4; ++i) { CHECK(ink[i] > 10); }
    CHECK(gray > 20);
    /* Visual proof artifact uses system fonts, so exact pixel goldens are not
     * portable across Windows font revisions. Advances are checked separately. */
    char path[64]; (void)snprintf(path,sizeof(path),"font-visual-%u.ppm",dpi);
    FILE *file = NULL; CHECK(fopen_s(&file,path,"wb") == 0 && file);
    CHECK(fprintf(file,"P6\n%u %u\n255\n",width,height) > 0);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) { CHECK(fwrite(pixels + ((size_t)y * width + x) * 4,1,3,file) == 3); }
    }
    CHECK(fclose(file) == 0);
    uint32_t serial = ui_renderer_stats(&renderer).generation;
    uint64_t updates = ui_renderer_stats(&renderer).texture_updates;
    for (unsigned i = 0; i < 1000; ++i) { CHECK(font_upload_renderer(&cache,&renderer) == FONT_OK); }
    CHECK(ui_renderer_stats(&renderer).generation == serial && ui_renderer_stats(&renderer).textures == 1);
    CHECK(ui_renderer_stats(&renderer).texture_updates == updates);
    FontHandle added; FontConfig more = {.size=16,.dpi=(uint16_t)dpi,.weight=400};
    CHECK(font_prepare_text(&cache,slice("atlas update"),&more,&added) == FONT_OK);
    CHECK(font_upload_renderer(&cache,&renderer) == FONT_OK);
    CHECK(ui_renderer_stats(&renderer).texture_updates == updates + 1 && ui_renderer_stats(&renderer).generation == serial);
    puts(path);
}

int main(void)
{
    UiRendererConfig config = {.width=256,.height=128,.warp=true,.debug=true};
    UiRenderResult result = ui_renderer_create(&renderer,&config);
    if (result == UI_RENDER_UNSUPPORTED) { puts("SKIP: WARP unavailable for font fixtures"); return 77; }
    CHECK(result == UI_RENDER_OK); CHECK(font_cache_create(&cache,NULL,NULL) == FONT_OK);
    visual(96); visual(144); visual(192);
    UiTexture old = cache.texture;
    ui_renderer_invalidate(&renderer); CHECK(ui_renderer_recover(&renderer) == UI_RENDER_OK);
    CHECK(font_upload_renderer(&cache,&renderer) == FONT_OK && cache.texture != old);
    CHECK(ui_renderer_stats(&renderer).textures == 1);
    font_cache_destroy(&cache); CHECK(font_stats(&cache).com_objects == 0);
    CHECK(ui_renderer_stats(&renderer).textures == 0); CHECK(ui_renderer_debug_report(&renderer));
    puts("font GPU: multilingual R8 coverage, clipping, DPI, reuse, recovery and resource teardown passed");
    return 0;
}
