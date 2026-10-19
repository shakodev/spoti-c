#include "renderer.h"
#include <math.h>
#include <string.h>

static UiRenderResult fail(UiRenderFrame *f, UiRenderResult result)
{
    if (f && f->error == UI_RENDER_OK) { f->error = result; }
    return f ? f->error : UI_RENDER_INVALID;
}
static bool rect_valid(UiRect r)
{
    return isfinite(r.x) && isfinite(r.y) && isfinite(r.w) && isfinite(r.h) &&
        fabsf(r.x) <= 1000000 && fabsf(r.y) <= 1000000 &&
        r.w >= 0 && r.w <= 1000000 && r.h >= 0 && r.h <= 1000000;
}

UiRenderResult ui_frame_begin(UiRenderFrame *f, uint32_t width, uint32_t height, float scale)
{
    if (!f) { return UI_RENDER_INVALID; }
    f->quad_count = f->batch_count = 0; f->clip_count = 1;
    f->width = width; f->height = height; f->scale = scale; f->error = UI_RENDER_OK;
    if (!width || !height || width > UI_MAX_SURFACE || height > UI_MAX_SURFACE ||
        !isfinite(scale) || scale < 0.25f || scale > 8) { return fail(f, UI_RENDER_INVALID); }
    f->clips[0] = (UiRect){0, 0, (float)width / scale, (float)height / scale};
    return UI_RENDER_OK;
}

UiRenderResult ui_frame_clip_push(UiRenderFrame *f, UiRect clip, bool horizontal, bool vertical)
{
    if (!f || f->error != UI_RENDER_OK) { return f ? f->error : UI_RENDER_INVALID; }
    if (!f->clip_count || !rect_valid(clip)) { return fail(f, UI_RENDER_INVALID); }
    if (f->clip_count == UI_MAX_CLIPS) { return fail(f, UI_RENDER_FULL); }
    UiRect old = f->clips[f->clip_count - 1], next = old;
    if (horizontal) {
        next.x = fmaxf(old.x, clip.x);
        next.w = fmaxf(0, fminf(old.x + old.w, clip.x + clip.w) - next.x);
    }
    if (vertical) {
        next.y = fmaxf(old.y, clip.y);
        next.h = fmaxf(0, fminf(old.y + old.h, clip.y + clip.h) - next.y);
    }
    /* Empty out-of-root clips must not overflow integer scissor conversion. */
    next.x = fminf(fmaxf(next.x, old.x), old.x + old.w);
    next.y = fminf(fmaxf(next.y, old.y), old.y + old.h);
    f->clips[f->clip_count++] = next;
    return UI_RENDER_OK;
}

UiRenderResult ui_frame_clip_pop(UiRenderFrame *f)
{
    if (!f || f->error != UI_RENDER_OK) { return f ? f->error : UI_RENDER_INVALID; }
    if (f->clip_count <= 1) { return fail(f, UI_RENDER_INVALID); }
    --f->clip_count; return UI_RENDER_OK;
}

UiRenderResult ui_frame_quad(UiRenderFrame *f, const QuadInstance *input, UiTexture texture)
{
    if (!f || f->error != UI_RENDER_OK) { return f ? f->error : UI_RENDER_INVALID; }
    if (!input || !f->clip_count) { return fail(f, UI_RENDER_INVALID); }
    QuadInstance q = *input;
    UiRect box = {q.box[0], q.box[1], q.box[2], q.box[3]};
    if (!rect_valid(box)) { return fail(f, UI_RENDER_INVALID); }
    for (unsigned i = 0; i < 4; ++i) {
        if (!isfinite(q.color[i]) || q.color[i] < 0 || q.color[i] > 1 ||
            !isfinite(q.uv[i]) || q.uv[i] < 0 || q.uv[i] > 1 ||
            !isfinite(q.radii[i]) || q.radii[i] < 0 ||
            !isfinite(q.border[i]) || q.border[i] < 0 ||
            !isfinite(q.effect[i])) { return fail(f, UI_RENDER_INVALID); }
        q.radii[i] = fminf(q.radii[i], fminf(box.w, box.h) * 0.5f);
        q.border[i] = fminf(q.border[i], (i == 0 || i == 2) ? box.w : box.h);
    }
    if (q.effect[0] < UI_QUAD_SOLID || q.effect[0] > UI_QUAD_GLYPH ||
        floorf(q.effect[0]) != q.effect[0] || q.effect[1] < 0 || q.effect[1] > 64 ||
        (q.effect[0] >= UI_QUAD_IMAGE ? texture == 0 : texture != 0)) {
        return fail(f, UI_RENDER_INVALID);
    }
    UiRect clip = f->clips[f->clip_count - 1];
    if (!box.w || !box.h || !clip.w || !clip.h || !q.color[3]) { return UI_RENDER_OK; }
    int32_t scissor[4] = {(int32_t)floorf(clip.x * f->scale), (int32_t)floorf(clip.y * f->scale),
        (int32_t)ceilf((clip.x + clip.w) * f->scale), (int32_t)ceilf((clip.y + clip.h) * f->scale)};
    if (scissor[2] > (int32_t)f->width) { scissor[2] = (int32_t)f->width; }
    if (scissor[3] > (int32_t)f->height) { scissor[3] = (int32_t)f->height; }
    UiBatch *batch = f->batch_count ? &f->batches[f->batch_count - 1] : NULL;
    bool split = !batch || batch->texture != texture || memcmp(batch->scissor, scissor, sizeof(scissor));
    if (f->quad_count == UI_MAX_QUADS || (split && f->batch_count == UI_MAX_BATCHES)) {
        return fail(f, UI_RENDER_FULL);
    }
    if (split) {
        batch = &f->batches[f->batch_count++];
        *batch = (UiBatch){.first = f->quad_count, .texture = texture};
        memcpy(batch->scissor, scissor, sizeof(scissor));
    }
    f->quads[f->quad_count++] = q; ++batch->count;
    return UI_RENDER_OK;
}

UiRenderResult ui_frame_dispatch(const UiRenderFrame *f, UiBatchSink sink, void *context)
{
    if (!f || !sink) { return UI_RENDER_INVALID; }
    if (f->error != UI_RENDER_OK) { return f->error; }
    if (f->clip_count != 1 || f->quad_count > UI_MAX_QUADS || f->batch_count > UI_MAX_BATCHES) {
        return UI_RENDER_INVALID;
    }
    uint32_t next = 0;
    for (uint32_t i = 0; i < f->batch_count; ++i) {
        const UiBatch *b = &f->batches[i];
        if (b->first != next || !b->count || b->count > f->quad_count - next) { return UI_RENDER_INVALID; }
        next += b->count;
    }
    if (next != f->quad_count) { return UI_RENDER_INVALID; }
    for (uint32_t i = 0; i < f->batch_count; ++i) {
        UiRenderResult result = sink(context, f, &f->batches[i]);
        if (result != UI_RENDER_OK) { return result; }
    }
    return UI_RENDER_OK;
}
