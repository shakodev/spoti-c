#include "ui/artwork.h"
#include "support/test_check.h"
#include <string.h>

static UiRenderer renderer;
static UiRenderFrame frame;
static TextureCache cache;
static unsigned char rgba[ART_UPLOAD_BYTES], readback[32 * 32 * 4];

static ArtworkId id(unsigned value)
{
    ArtworkId result = {.size = 256, .generation = 1};
    memcpy(result.content, &value, sizeof(value));
    return result;
}

static void draw(ArtworkHandle handle, bool check_pixel)
{
    UiRendererStats before = ui_renderer_stats(&renderer);
    uint64_t uploads = cache.uploads, releases = cache.releases;
    UiTexture texture;
    CHECK(texture_cache_resolve(&cache, handle, &texture));
    CHECK(ui_frame_begin(&frame, 32, 32, 1) == UI_RENDER_OK);
    QuadInstance quad = {.box = {0,0,32,32}, .color = {1,1,1,1},
        .uv = {0,0,1,1}, .effect = {UI_QUAD_IMAGE,0,0,0}};
    CHECK(ui_frame_quad(&frame, &quad, texture) == UI_RENDER_OK);
    UiRendererStats built = ui_renderer_stats(&renderer);
    CHECK(cache.uploads == uploads && cache.releases == releases);
    CHECK(built.resources == before.resources && built.textures == before.textures);
    CHECK(built.owned_gpu_bytes == before.owned_gpu_bytes && built.maps == before.maps);
    const float clear[4] = {0};
    CHECK(ui_renderer_submit(&renderer, &frame, clear) == UI_RENDER_OK);
    UiRendererStats submitted = ui_renderer_stats(&renderer);
    CHECK(submitted.resources == before.resources && submitted.textures == before.textures);
    CHECK(submitted.owned_gpu_bytes == before.owned_gpu_bytes);
    if (check_pixel) {
        CHECK(ui_renderer_readback(&renderer, readback, 32 * 4, sizeof(readback)) == UI_RENDER_OK);
        for (unsigned i = 0; i < sizeof(readback); ++i) {
            const unsigned char expected[4] = {32,96,160,255};
            CHECK(readback[i] == expected[i % 4]);
        }
    }
}

int main(void)
{
    UiRendererConfig config = {.width = 32, .height = 32, .warp = true, .debug = true};
    UiRenderResult result = ui_renderer_create(&renderer, &config);
    if (result == UI_RENDER_UNSUPPORTED) { puts("SKIP: WARP artwork device unavailable"); return 77; }
    CHECK(result == UI_RENDER_OK);
    UiRendererStats baseline = ui_renderer_stats(&renderer);
    texture_cache_init(&cache, artwork_renderer_gpu(&renderer));
    for (unsigned at = 0; at < sizeof(rgba); at += 4) {
        rgba[at] = 32; rgba[at + 1] = 96; rgba[at + 2] = 160; rgba[at + 3] = 255;
    }
    enum { BYTE_CAP_TEXTURES=ART_GPU_BYTES/(256u*256u*4u) };
    _Static_assert(BYTE_CAP_TEXTURES<UI_MAX_TEXTURES,"byte cap must be independent of renderer descriptors");
    ArtworkHandle handles[BYTE_CAP_TEXTURES], current;
    for (unsigned i = 0; i < BYTE_CAP_TEXTURES; ++i) {
        CHECK(texture_cache_insert(&cache, id(i), 256, 256, rgba, handles + i) == ART_OK);
    }
    CHECK(cache.bytes == ART_GPU_BYTES && cache.peak_bytes == ART_GPU_BYTES);
    CHECK(ui_renderer_stats(&renderer).textures == BYTE_CAP_TEXTURES);
    uint32_t resources = ui_renderer_stats(&renderer).resources;
    CHECK(texture_cache_insert(&cache, id(100), 256, 256, rgba, &current) == ART_FULL);
    CHECK(ui_renderer_stats(&renderer).resources == resources && cache.releases == 0);
    for (unsigned i = 0; i < BYTE_CAP_TEXTURES; ++i) {
        CHECK(!cache.entries[i].retired); /* All visible textures are pinned. */
    }
    draw(handles[0], true);
    texture_cache_prepare(&cache);
    CHECK(texture_cache_insert(&cache, id(100), 256, 256, rgba, &current) == ART_FULL);
    UiTexture retired = cache.entries[1].texture;
    CHECK(cache.entries[1].retired && ui_renderer_texture_valid(&renderer, retired));
    CHECK(!texture_cache_resolve(&cache, handles[1], &retired));
    CHECK(ui_renderer_stats(&renderer).resources == resources); /* Deferred release. */
    texture_cache_prepare(&cache);
    CHECK(ui_renderer_stats(&renderer).resources + 1 == resources);
    CHECK(texture_cache_insert(&cache, id(100), 256, 256, rgba, &current) == ART_OK);
    CHECK(!texture_cache_resolve(&cache, handles[1], &retired));
    for (unsigned i = 101; i < 501; ++i) {
        texture_cache_prepare(&cache);
        CHECK(texture_cache_insert(&cache, id(i), 256, 256, rgba, &current) == ART_FULL);
        texture_cache_prepare(&cache);
        CHECK(texture_cache_insert(&cache, id(i), 256, 256, rgba, &current) == ART_OK);
        CHECK(cache.bytes <= ART_GPU_BYTES && cache.peak_bytes <= ART_GPU_BYTES);
        CHECK(ui_renderer_stats(&renderer).textures == BYTE_CAP_TEXTURES);
        draw(current, i % 100 == 0);
    }
    UiTexture previous;
    CHECK(texture_cache_resolve(&cache, current, &previous));
    ui_renderer_invalidate(&renderer);
    CHECK(ui_renderer_recover(&renderer) == UI_RENDER_OK);
    texture_cache_prepare(&cache);
    CHECK(cache.bytes == 0 && !texture_cache_resolve(&cache, current, &previous));
    CHECK(texture_cache_insert(&cache, id(999), 256, 256, rgba, &current) == ART_OK);
    draw(current, true);
    texture_cache_clear(&cache);
    CHECK(cache.bytes == 0 && cache.uploads == cache.releases);
    UiRendererStats after = ui_renderer_stats(&renderer);
    CHECK(after.textures == baseline.textures && after.resources == baseline.resources);
    CHECK(after.owned_gpu_bytes == baseline.owned_gpu_bytes);
    CHECK(ui_renderer_debug_report(&renderer));
    puts("artwork WARP: 400 navigation evictions, 4 MiB cap, pins, deferred release, stale handles and DXGI teardown passed");
    return 0;
}
