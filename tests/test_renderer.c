#include "ui/renderer_clay.h"
#include "support/test_check.h"
#include <math.h>
#include <string.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static bool watch_heap;
static unsigned heap_calls;
static int __cdecl allocation_hook(int type, void *data, size_t bytes, int block,
    long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)bytes; (void)block; (void)request; (void)file; (void)line;
    if (watch_heap) { ++heap_calls; } return 1;
}
#endif

static UiRenderFrame frame;
static QuadInstance quad(void)
{
    QuadInstance q = {0}; q.box[2] = q.box[3] = 20;
    q.color[0] = q.color[3] = 1; return q;
}
typedef struct FakeGpu { uint32_t next, draws; UiTexture textures[8]; bool reject; } FakeGpu;
static UiRenderResult draw(void *context, const UiRenderFrame *f, const UiBatch *b)
{
    FakeGpu *gpu = context;
    CHECK(b->first == gpu->next && b->first + b->count <= f->quad_count);
    if (gpu->reject) { return UI_RENDER_DEVICE_LOST; }
    CHECK(gpu->draws < 8); gpu->textures[gpu->draws++] = b->texture;
    gpu->next += b->count; return UI_RENDER_OK;
}

static void test_clips_order(void)
{
    for (unsigned dpi = 1; dpi <= 3; ++dpi) {
        float scale = (float)dpi * 0.75f; QuadInstance q = quad();
        CHECK(ui_frame_begin(&frame, 100, 100, scale) == UI_RENDER_OK);
        CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK);
        CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK);
        CHECK(frame.batch_count == 1 && frame.batches[0].count == 2);
        CHECK(ui_frame_clip_push(&frame, (UiRect){2,3,20,30}, true, true) == UI_RENDER_OK);
        CHECK(ui_frame_clip_push(&frame, (UiRect){4,6,5,8}, true, false) == UI_RENDER_OK);
        q.color[3] = 0.5f; CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK);
        CHECK(frame.batches[1].scissor[0] == (int32_t)floorf(4 * scale));
        CHECK(frame.batches[1].scissor[1] == (int32_t)floorf(3 * scale));
        CHECK(frame.batches[1].scissor[2] == (int32_t)ceilf(9 * scale));
        CHECK(ui_frame_clip_pop(&frame) == UI_RENDER_OK); CHECK(ui_frame_clip_pop(&frame) == UI_RENDER_OK);
        q.effect[0] = UI_QUAD_IMAGE; q.uv[2] = q.uv[3] = 1;
        CHECK(ui_frame_quad(&frame, &q, 257) == UI_RENDER_OK);
        CHECK(ui_frame_quad(&frame, &q, 258) == UI_RENDER_OK);
        CHECK(ui_frame_quad(&frame, &q, 257) == UI_RENDER_OK);
        FakeGpu gpu = {0}; CHECK(ui_frame_dispatch(&frame, draw, &gpu) == UI_RENDER_OK);
        CHECK(gpu.next == 6 && gpu.draws == 5);
        CHECK(gpu.textures[2] == 257 && gpu.textures[3] == 258 && gpu.textures[4] == 257);
        gpu = (FakeGpu){.reject = true}; CHECK(ui_frame_dispatch(&frame, draw, &gpu) == UI_RENDER_DEVICE_LOST);
    }
}

static void test_limits(void)
{
    QuadInstance q = quad(); FakeGpu gpu = {0};
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    for (unsigned i = 0; i < UI_MAX_QUADS; ++i) { CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK); }
    CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_FULL);
    CHECK(frame.quad_count == UI_MAX_QUADS && frame.batch_count == 1);
    CHECK(ui_frame_dispatch(&frame, draw, &gpu) == UI_RENDER_FULL && gpu.draws == 0);
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    q.effect[0] = UI_QUAD_IMAGE;
    for (unsigned i = 0; i < UI_MAX_BATCHES; ++i) {
        CHECK(ui_frame_quad(&frame, &q, 257 + i % 2) == UI_RENDER_OK);
    }
    CHECK(ui_frame_quad(&frame, &q, 257) == UI_RENDER_FULL);
    CHECK(frame.quad_count == UI_MAX_BATCHES);
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    for (unsigned i = 1; i < UI_MAX_CLIPS; ++i) {
        CHECK(ui_frame_clip_push(&frame, (UiRect){0,0,100,100}, true, true) == UI_RENDER_OK);
    }
    CHECK(ui_frame_clip_push(&frame, (UiRect){0,0,10,10}, true, true) == UI_RENDER_FULL);
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    CHECK(ui_frame_clip_pop(&frame) == UI_RENDER_INVALID);
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    CHECK(ui_frame_clip_push(&frame, (UiRect){1000000,0,5,5}, true, true) == UI_RENDER_OK);
    q = quad(); CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_OK && frame.quad_count == 0);
    CHECK(ui_frame_dispatch(&frame, draw, &gpu) == UI_RENDER_INVALID); /* unbalanced */
    CHECK(ui_frame_begin(&frame, 100, 100, NAN) == UI_RENDER_INVALID);
    CHECK(ui_frame_begin(&frame, 0, 100, 1) == UI_RENDER_INVALID);
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    q.box[0] = INFINITY; CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_INVALID);
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    q = quad(); q.effect[0] = UI_QUAD_SHADOW; q.effect[1] = 65;
    CHECK(ui_frame_quad(&frame, &q, 0) == UI_RENDER_INVALID);
}

static UiRenderResult image(void *context, void *data, UiTexture *texture)
{
    CHECK(context == data); *texture = 257; return UI_RENDER_OK;
}
static void test_clay(void)
{
    Clay_RenderCommand c[6] = {0};
    c[0].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START;
    c[0].boundingBox = (Clay_BoundingBox){5,5,10,10}; c[0].renderData.clip.horizontal = true;
    c[0].renderData.clip.vertical = true;
    c[1].commandType = CLAY_RENDER_COMMAND_TYPE_RECTANGLE;
    c[1].boundingBox = (Clay_BoundingBox){0,0,20,20};
    c[1].renderData.rectangle.backgroundColor = (Clay_Color){255,128,0,128};
    c[1].renderData.rectangle.cornerRadius = (Clay_CornerRadius){2,3,4,5};
    c[2].commandType = CLAY_RENDER_COMMAND_TYPE_BORDER; c[2].boundingBox = c[1].boundingBox;
    c[2].renderData.border.color = (Clay_Color){0,0,255,255};
    c[2].renderData.border.width = (Clay_BorderWidth){1,2,3,4,0};
    c[3].commandType = CLAY_RENDER_COMMAND_TYPE_IMAGE; c[3].boundingBox = c[1].boundingBox;
    c[3].renderData.image.imageData = c;
    c[4].commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END;
    Clay_RenderCommandArray commands = {6,5,c}; UiClayBridge bridge = {image,NULL,c};
    CHECK(ui_frame_begin(&frame, 100, 100, 2) == UI_RENDER_OK);
    CHECK(ui_frame_clay(&frame, commands, &bridge) == UI_RENDER_OK);
    CHECK(frame.quad_count == 3 && frame.batch_count == 2 && frame.clip_count == 1);
    CHECK(frame.quads[0].radii[2] == 5 && frame.quads[0].radii[3] == 4);
    CHECK(frame.quads[1].border[0] == 1 && frame.quads[1].border[1] == 3);
    CHECK(frame.quads[1].border[2] == 2 && frame.quads[1].border[3] == 4);
    CHECK(frame.quads[2].color[0] == 1 && frame.quads[2].color[3] == 1);
    CHECK(frame.batches[0].scissor[0] == 10 && frame.batches[0].scissor[2] == 30);
    c[0].commandType = CLAY_RENDER_COMMAND_TYPE_TEXT; commands.length = 1;
    CHECK(ui_frame_begin(&frame, 100, 100, 1) == UI_RENDER_OK);
    CHECK(ui_frame_clay(&frame, commands, NULL) == UI_RENDER_UNSUPPORTED);
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(allocation_hook); watch_heap = true;
#endif
    test_clips_order(); test_limits(); test_clay();
#if defined(_MSC_VER) && defined(_DEBUG)
    watch_heap = false; CHECK(heap_calls == 0); _CrtSetAllocHook(NULL);
#endif
    puts("ui_renderer: fixed budgets, painter order, clipping, fake dispatch and Clay passed");
    return 0;
}
