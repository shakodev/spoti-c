#include "font_internal.h"
#include <string.h>
#include <math.h>

static uint32_t mask(unsigned width, unsigned x)
{
    return width == 32 ? UINT32_MAX : ((1u << width) - 1u) << x;
}
static void release_entry(FontCache *c, FontAtlasEntry *e)
{
    for (unsigned y = e->tile_y; y < (unsigned)e->tile_y + e->tile_h; ++y) { c->tiles[y] &= ~mask(e->tile_w,e->tile_x); }
    e->valid = false; --c->stats.atlas_entries;
}
static bool find_space(FontCache *c, unsigned width, unsigned height, unsigned *x, unsigned *y)
{
    for (*y = 0; *y + height <= 32; ++*y) {
        for (*x = 0; *x + width <= 32; ++*x) {
            uint32_t bits = mask(width,*x); unsigned row = *y;
            while (row < *y + height && !(c->tiles[row] & bits)) { ++row; }
            if (row == *y + height) { return true; }
        }
    }
    return false;
}
FontResult font_atlas_add(FontCache *c, FontRun *run, const RECT *bounds, const BYTE *rgb)
{
    unsigned width = (unsigned)(bounds->right - bounds->left), height = (unsigned)(bounds->bottom - bounds->top);
    if (!width || !height) { return FONT_OK; }
    if (width > 1022 || height > 1022 || run->patch_count == FONT_PATCHES ||
        c->serial == UINT64_MAX || c->epoch == UINT64_MAX) { return FONT_FULL; }
    unsigned tw = (width + 33) / 32, th = (height + 33) / 32, x = 0, y = 0, slot = 0;
    for (unsigned attempt = 0; attempt <= FONT_ATLAS_ENTRIES; ++attempt) {
        slot = 0; while (slot < FONT_ATLAS_ENTRIES && c->entries[slot].valid) { ++slot; }
        if (slot < FONT_ATLAS_ENTRIES && find_space(c,tw,th,&x,&y)) { break; }
        unsigned victim = FONT_ATLAS_ENTRIES;
        for (unsigned i = 0; i < FONT_ATLAS_ENTRIES; ++i) {
            FontAtlasEntry *e = &c->entries[i];
            if (e->valid && e->pinned != c->prepare_id &&
                (victim == FONT_ATLAS_ENTRIES || e->used < c->entries[victim].used)) { victim = i; }
        }
        if (victim == FONT_ATLAS_ENTRIES) { return FONT_FULL; }
        release_entry(c,&c->entries[victim]); ++c->stats.evictions;
    }
    if (slot == FONT_ATLAS_ENTRIES) { return FONT_FULL; }
    FontAtlasEntry *e = &c->entries[slot];
    *e = (FontAtlasEntry){.generation = ++c->serial,.used = c->clock,.pinned = c->prepare_id,
        .x = (uint16_t)(x * 32 + 1),.y = (uint16_t)(y * 32 + 1),.width = (uint16_t)width,.height = (uint16_t)height,
        .tile_x = (uint16_t)x,.tile_y = (uint16_t)y,.tile_w = (uint16_t)tw,.tile_h = (uint16_t)th,.valid = true};
    for (unsigned row = y; row < y + th; ++row) { c->tiles[row] |= mask(tw,x); }
    for (unsigned row = y * 32; row < (y + th) * 32; ++row) { memset(c->atlas + row * 1024 + x * 32,0,tw * 32); }
    for (unsigned row = 0; row < height; ++row) {
        for (unsigned col = 0; col < width; ++col) {
            size_t at = ((size_t)row * width + col) * 3;
            c->atlas[((size_t)e->y + row) * 1024 + e->x + col] =
                (unsigned char)(((unsigned)rgb[at] + rgb[at + 1] + rgb[at + 2] + 1) / 3);
        }
    }
    float scale = (float)run->config.dpi / 96;
    run->patches[run->patch_count++] = (FontPatch){e->generation,(uint16_t)slot,
        (float)bounds->left / scale,(float)bounds->top / scale};
    ++c->stats.atlas_entries; ++c->epoch; return FONT_OK;
}

FontResult font_cache_reset(FontCache *c)
{
    if (!font_owner(c)) { return FONT_INVALID; }
    if (c->epoch == UINT64_MAX) { return FONT_FULL; }
    for (unsigned i = 0; i < FONT_RUNS; ++i) { c->runs[i].valid = false; }
    memset(c->tiles,0,sizeof(c->tiles)); memset(c->entries,0,sizeof(c->entries)); memset(c->atlas,0,sizeof(c->atlas));
    c->stats.atlas_entries = c->stats.pending = 0; ++c->epoch; ++c->stats.resets;
    font_invalidate_clay(c); return FONT_OK;
}
FontResult font_atlas_commit(FontCache *c, UiTexture texture)
{
    if (!font_owner(c) || !texture || (c->gpu_owner && c->texture != texture)) { return FONT_INVALID; }
    c->texture = texture; c->uploaded = c->epoch; return FONT_OK;
}
FontResult font_upload_renderer(FontCache *c, UiRenderer *r)
{
    if (!font_owner(c) || !r || (c->gpu_owner && c->gpu_owner != r)) { return FONT_INVALID; }
    if (c->gpu_owner && c->uploaded == c->epoch && ui_renderer_texture_valid(r,c->texture)) { return FONT_OK; }
    UiRenderResult result = UI_RENDER_INVALID;
    if (c->texture && c->gpu_owner) {
        result = ui_renderer_texture_update_r8(r,c->texture,c->atlas,1024);
        if (result == UI_RENDER_INVALID) { c->texture = 0; }
    }
    if (!c->texture) { result = ui_renderer_texture_r8(r,1024,1024,c->atlas,1024,&c->texture); }
    if (result != UI_RENDER_OK) { return result == UI_RENDER_FULL ? FONT_FULL : FONT_OS_ERROR; }
    c->gpu_owner = r; return font_atlas_commit(c,c->texture);
}
FontResult font_render_text(FontCache *c, FontHandle handle, UiRenderFrame *f, float x, float y, const float color[4])
{
    if (!font_owner(c) || !f || !color || !isfinite(x) || !isfinite(y)) { return FONT_INVALID; }
    FontMetrics metrics;
    FontResult result = font_metrics(c,handle,&metrics);
    if (result != FONT_OK) { return result; }
    FontRun *run = &c->runs[handle.slot];
    if (run->patch_count && (!c->texture || c->uploaded != c->epoch)) { return FONT_PENDING_WORK; }
    float scale = (float)run->config.dpi / 96;
    if (run->config.ellipsis && ui_frame_clip_push(f,(UiRect){x,y,run->config.max_width,metrics.height},true,true) != UI_RENDER_OK) {
        return FONT_FULL;
    }
    for (uint32_t i = 0; i < run->patch_count; ++i) {
        FontPatch *p = &run->patches[i]; FontAtlasEntry *e = &c->entries[p->entry];
        QuadInstance q = {.box = {x + p->x,y + p->y,(float)e->width / scale,(float)e->height / scale},
            .uv = {(float)e->x / 1024,(float)e->y / 1024,(float)(e->x + e->width) / 1024,(float)(e->y + e->height) / 1024},
            .effect = {UI_QUAD_GLYPH,0,0,0}};
        memcpy(q.color,color,sizeof(q.color));
        UiRenderResult draw = ui_frame_quad(f,&q,c->texture);
        if (draw != UI_RENDER_OK) { return draw == UI_RENDER_FULL ? FONT_FULL : FONT_INVALID; }
    }
    if (run->config.ellipsis && ui_frame_clip_pop(f) != UI_RENDER_OK) { return FONT_INVALID; }
    return FONT_OK;
}
