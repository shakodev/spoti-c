#include "font_internal.h"
#include "font_roles.h"
#include <math.h>
#include <string.h>

bool font_owner(const FontCache *c) { return c && c->factory && c->owner == GetCurrentThreadId(); }
bool font_config_valid(const FontConfig *c)
{
    return c && c->face <= 1 && c->size > 0 && c->size <= 96 && c->dpi >= 48 && c->dpi <= 384 &&
        c->weight >= 100 && c->weight <= 900 && isfinite(c->letter_spacing) && c->letter_spacing >= 0 &&
        c->letter_spacing <= 32 && isfinite(c->line_height) && c->line_height >= 0 && c->line_height <= 256 &&
        isfinite(c->max_width) && c->max_width >= 0 && c->max_width <= 65536 && (!c->ellipsis || c->max_width > 0);
}
static bool config_equal(const FontConfig *a, const FontConfig *b)
{
    return a->face == b->face && a->size == b->size && a->dpi == b->dpi && a->weight == b->weight &&
        a->letter_spacing == b->letter_spacing && a->line_height == b->line_height &&
        a->max_width == b->max_width && a->text_generation == b->text_generation &&
        a->italic == b->italic && a->rtl == b->rtl && a->ellipsis == b->ellipsis && a->measure_only == b->measure_only;
}
bool font_key(const FontRun *r, Clay_StringSlice text, const FontConfig *config)
{
    return r->valid && text.length >= 0 && r->length == (uint32_t)text.length &&
        config_equal(&r->config,config) && (!r->length || !memcmp(r->text,text.chars,r->length));
}
bool font_run_valid(const FontCache *c, const FontRun *r)
{
    if (!r->valid || r->failure != FONT_OK) { return false; }
    for (uint32_t i = 0; i < r->patch_count; ++i) {
        const FontPatch *p = &r->patches[i];
        if (p->entry >= FONT_ATLAS_ENTRIES || !c->entries[p->entry].valid ||
            c->entries[p->entry].generation != p->generation) { return false; }
    }
    return true;
}
void font_touch(FontCache *c, FontRun *r)
{
    if (c->clock < UINT64_MAX) { ++c->clock; }
    r->used = c->clock;
    for (uint32_t i = 0; i < r->patch_count; ++i) { c->entries[r->patches[i].entry].used = c->clock; }
}

FontResult font_lookup(FontCache *c, Clay_StringSlice text, const FontConfig *config, FontHandle *out)
{
    if (out) { *out = (FontHandle){0}; }
    if (!font_owner(c) || !out || !font_config_valid(config) || text.length < 0 ||
        text.length > (int32_t)FONT_TEXT_BYTES || (text.length && !text.chars)) { return FONT_INVALID; }
    for (uint32_t i = 0; i < FONT_RUNS; ++i) {
        if (font_key(&c->runs[i],text,config) && c->runs[i].failure != FONT_OK) { return c->runs[i].failure; }
        if (font_key(&c->runs[i],text,config) && font_run_valid(c,&c->runs[i])) {
            font_touch(c,&c->runs[i]); *out = (FontHandle){i,c->runs[i].generation}; ++c->stats.hits; return FONT_OK;
        }
    }
    return FONT_STALE;
}

FontResult font_request_text(FontCache *c, Clay_StringSlice text, const FontConfig *config)
{
    FontHandle handle;
    FontResult result = font_lookup(c,text,config,&handle);
    if (result != FONT_STALE) { return result; }
    uint32_t units;
    result = font_utf8_to_utf16(text,NULL,0,&units);
    if (result != FONT_OK) { return result; }
    for (uint32_t i = 0; i < c->stats.pending; ++i) {
        FontRequest *p = &c->pending[i];
        if (p->length == (uint32_t)text.length && config_equal(&p->config,config) &&
            (!p->length || !memcmp(p->text,text.chars,p->length))) { return FONT_PENDING_WORK; }
    }
    if (c->stats.pending == FONT_PENDING) { return FONT_FULL; }
    bool first = c->stats.pending == 0;
    FontRequest *p = &c->pending[c->stats.pending++];
    p->config = *config; p->length = (uint32_t)text.length;
    if (p->length) { memcpy(p->text,text.chars,p->length); }
    ++c->stats.misses;
    if (first && c->wake) { ++c->stats.wakes; c->wake(c->wake_context); }
    return FONT_PENDING_WORK;
}

FontResult font_prepare_text(FontCache *c, Clay_StringSlice text, const FontConfig *config, FontHandle *out)
{
    FontResult result = font_lookup(c,text,config,out);
    if (result != FONT_STALE) { return result; }
    uint32_t units;
    result = font_utf8_to_utf16(text,c->utf16,FONT_TEXT_BYTES + 1,&units);
    if (result != FONT_OK) { return result; }
    if (c->serial == UINT64_MAX || c->prepare_id == UINT64_MAX) { return FONT_FULL; }
    uint32_t slot = 0;
    for (uint32_t i = 0; i < FONT_RUNS; ++i) {
        if (!c->runs[i].valid) { slot = i; break; }
        if (c->runs[i].used < c->runs[slot].used) { slot = i; }
    }
    FontRun *r = &c->runs[slot]; r->valid = false; r->patch_count = 0; r->failure = FONT_OK;
    r->generation = ++c->serial; ++c->prepare_id;
    r->config = *config; r->length = (uint32_t)text.length; r->metrics = (FontMetrics){.utf16_units = units};
    if (r->length) { memcpy(r->text,text.chars,r->length); }
    result = font_shape(c,r);
    if (result != FONT_OK) {
        r->failure = result; r->valid = true; r->patch_count = 0; font_touch(c,r); return result;
    }
    r->valid = true; font_touch(c,r); ++c->stats.prepared; font_invalidate_clay(c);
    *out = (FontHandle){slot,r->generation}; return FONT_OK;
}

FontResult font_prepare_pending(FontCache *c)
{
    if (!font_owner(c)) { return FONT_INVALID; }
    FontResult result = FONT_OK;
    uint32_t count = c->stats.pending;
    for (uint32_t i = 0; i < count; ++i) {
        FontHandle handle; FontRequest *p = &c->pending[i];
        FontResult current = font_prepare_text(c,(Clay_StringSlice){(int32_t)p->length,p->text,p->text},&p->config,&handle);
        if (current != FONT_OK && result == FONT_OK) { result = current; }
    }
    c->stats.pending = 0; return result;
}

FontResult font_metrics(FontCache *c, FontHandle handle, FontMetrics *out)
{
    if (!font_owner(c) || !out) { return FONT_INVALID; }
    *out = (FontMetrics){0};
    if (handle.slot >= FONT_RUNS || c->runs[handle.slot].generation != handle.generation ||
        !font_run_valid(c,&c->runs[handle.slot])) { return FONT_STALE; }
    font_touch(c,&c->runs[handle.slot]); *out = c->runs[handle.slot].metrics; return FONT_OK;
}

FontConfig font_clay_config(const FontMeasureContext *context, uint16_t face, uint16_t size, uint16_t spacing, uint16_t height)
{
    return font_config_for_clay(context->config,face,size,spacing,height);
}
Clay_Dimensions font_measure(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data)
{
    FontMeasureContext *ctx = user_data;
    if (!ctx || !config) { return (Clay_Dimensions){0}; }
    FontConfig key = font_clay_config(ctx,config->fontId,config->fontSize,config->letterSpacing,config->lineHeight);
    key.measure_only=true;
    FontHandle handle; FontMetrics metrics;
    ctx->last_result = font_lookup(ctx->cache,text,&key,&handle);
    if (ctx->last_result == FONT_OK) {
        ctx->last_result = font_metrics(ctx->cache,handle,&metrics);
        return (Clay_Dimensions){metrics.width,metrics.height};
    }
    ctx->last_result = font_request_text(ctx->cache,text,&key);
    return (Clay_Dimensions){0,key.line_height > 0 ? key.line_height : (float)key.size};
}

UiRenderResult font_render_clay(void *context, UiRenderFrame *frame, const Clay_RenderCommand *command)
{
    FontMeasureContext *ctx = context;
    if (!ctx || !command || command->commandType != CLAY_RENDER_COMMAND_TYPE_TEXT) { return UI_RENDER_UNSUPPORTED; }
    const Clay_TextRenderData *text = &command->renderData.text;
    FontConfig key = font_clay_config(ctx,text->fontId,text->fontSize,text->letterSpacing,text->lineHeight);
    FontHandle handle; ctx->last_result = font_lookup(ctx->cache,text->stringContents,&key,&handle);
    if (ctx->last_result == FONT_OK) {
        const float color[4] = {text->textColor.r / 255,text->textColor.g / 255,text->textColor.b / 255,text->textColor.a / 255};
        ctx->last_result = font_render_text(ctx->cache,handle,frame,command->boundingBox.x,command->boundingBox.y,color);
    }
    if (ctx->last_result == FONT_STALE) { ctx->last_result = font_request_text(ctx->cache,text->stringContents,&key); }
    if (ctx->last_result == FONT_OK || ctx->last_result == FONT_PENDING_WORK) { return UI_RENDER_OK; }
    return ctx->last_result == FONT_FULL ? UI_RENDER_FULL : UI_RENDER_INVALID;
}
FontStats font_stats(const FontCache *c) { return c ? c->stats : (FontStats){0}; }
void font_invalidate_clay(FontCache *cache)
{
    if (cache->clay_context) {
        Clay_Context *before = Clay_GetCurrentContext();
        Clay_SetCurrentContext(cache->clay_context); Clay_ResetMeasureTextCache(); Clay_SetCurrentContext(before);
    }
}
void font_bind_clay(FontMeasureContext *context)
{
    if (!context || !font_owner(context->cache) || !Clay_GetCurrentContext()) { return; }
    context->cache->clay_context = Clay_GetCurrentContext();
    Clay_SetMeasureTextFunction(font_measure,context); font_invalidate_clay(context->cache);
}
