#include "artwork.h"
#include <string.h>
bool artwork_id_equal(ArtworkId a, ArtworkId b)
{
    return a.size == b.size && a.generation == b.generation && !memcmp(a.content, b.content, sizeof a.content);
}
void texture_cache_init(TextureCache *c, ArtworkGpu gpu)
{
    *c = (TextureCache){.gpu = gpu};
}
static void release_entry(TextureCache *c, TextureEntry *e)
{
    if (!e->texture) return;
    c->gpu.release(c->gpu.context, e->texture);
    c->bytes -= e->bytes; ++c->releases;
    *e = (TextureEntry){0};
}
void texture_cache_collect(TextureCache *c)
{
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) {
        TextureEntry *e = &c->entries[i];
        if (e->texture && (e->retired || !c->gpu.valid(c->gpu.context, e->texture))) release_entry(c, e);
    }
}
void texture_cache_prepare(TextureCache *c)
{
    texture_cache_collect(c);
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) c->entries[i].pinned = false;
}
bool texture_cache_find(TextureCache *c, ArtworkId id, ArtworkHandle *out)
{
    if (!c || !out) return false;
    *out = (ArtworkHandle){0};
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) {
        TextureEntry *e = &c->entries[i];
        if (e->texture && !e->retired && artwork_id_equal(e->id, id) && c->gpu.valid(c->gpu.context, e->texture)) {
            e->pinned = true; e->touched = ++c->clock;
            *out = (ArtworkHandle){i + 1, e->serial}; return true;
        }
    }
    return false;
}
bool texture_cache_resolve(TextureCache *c, ArtworkHandle h, UiTexture *out)
{
    if (out) *out = 0;
    if (!c || !out || !h.slot || h.slot > ART_CACHE_SLOTS) return false;
    TextureEntry *e = &c->entries[h.slot - 1];
    if (!e->texture || e->retired || h.serial != e->serial || !c->gpu.valid(c->gpu.context, e->texture)) return false;
    *out = e->texture; e->pinned = true; e->touched = ++c->clock; return true;
}
ArtResult texture_cache_insert(TextureCache *c, ArtworkId id, uint32_t w,
    uint32_t h, const unsigned char *rgba, ArtworkHandle *out)
{
    if (out) *out = (ArtworkHandle){0};
    if (!c || !out || !rgba || !w || !h || w > ART_MAX_EDGE || h > ART_MAX_EDGE ||
        !id.size || id.size > ART_MAX_EDGE || !c->gpu.upload || !c->gpu.release || !c->gpu.valid) return ART_INVALID;
    if (texture_cache_find(c, id, out)) return ART_OK;
    uint32_t bytes = w * h * 4u;
    TextureEntry *free_entry = NULL, *oldest = NULL;
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) {
        TextureEntry *e = &c->entries[i];
        if (!e->texture && !free_entry) free_entry = e;
        if (e->texture && !e->pinned && !e->retired && (!oldest || e->touched < oldest->touched)) oldest = e;
    }
    if (!free_entry || bytes > ART_GPU_BYTES - c->bytes) {
        if (oldest) oldest->retired = true;
        return ART_FULL;
    }
    if (c->serial == UINT64_MAX) return ART_FULL;
    UiTexture texture = 0;
    ArtResult r = c->gpu.upload(c->gpu.context, w, h, rgba, &texture);
    if (r != ART_OK) {
        /* The shared renderer may have fewer free slots than this cache. */
        if (r == ART_FULL && oldest) oldest->retired = true;
        return r;
    }
    *free_entry = (TextureEntry){.id = id, .texture = texture, .bytes = bytes, .width = w, .height = h,
        .serial = ++c->serial, .touched = ++c->clock, .pinned = true};
    c->bytes += bytes; ++c->uploads;
    if (c->bytes > c->peak_bytes) c->peak_bytes = c->bytes;
    *out = (ArtworkHandle){(uint32_t)(free_entry - c->entries) + 1, free_entry->serial};
    return ART_OK;
}
void texture_cache_clear(TextureCache *c)
{
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) release_entry(c, &c->entries[i]);
}
static ArtResult renderer_upload(void *ctx, uint32_t w, uint32_t h, const unsigned char *rgba, UiTexture *out)
{
    UiRenderResult r = ui_renderer_texture(ctx, w, h, rgba, w * 4u, out);
    /* Device recovery is scheduled by the surface owner. Keep the decoded
     * upload pending across that boundary instead of caching a content error. */
    return r == UI_RENDER_OK ? ART_OK :
        r == UI_RENDER_FULL || r == UI_RENDER_DEVICE_LOST ? ART_FULL : ART_IO_ERROR;
}
static void renderer_release(void *ctx, UiTexture texture) { (void)ui_renderer_texture_release(ctx, texture); }
static bool renderer_valid(void *ctx, UiTexture texture) { return ui_renderer_texture_valid(ctx, texture); }
ArtworkGpu artwork_renderer_gpu(UiRenderer *renderer)
{
    return (ArtworkGpu){renderer_upload, renderer_release, renderer_valid, renderer};
}
