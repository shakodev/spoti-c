#include "renderer_clay.h"

static void color(float out[4], Clay_Color in)
{
    out[0] = in.r / 255; out[1] = in.g / 255; out[2] = in.b / 255; out[3] = in.a / 255;
}
static void radii(float out[4], Clay_CornerRadius in)
{
    out[0] = in.topLeft; out[1] = in.topRight; out[2] = in.bottomRight; out[3] = in.bottomLeft;
}

UiRenderResult ui_frame_clay(UiRenderFrame *f, Clay_RenderCommandArray commands, const UiClayBridge *bridge)
{
    if (!f) { return UI_RENDER_INVALID; }
    if (f->error != UI_RENDER_OK) { return f->error; }
    if (commands.length < 0 || commands.length > commands.capacity ||
        (commands.length && !commands.internalArray)) { return f->error = UI_RENDER_INVALID; }
    for (int32_t i = 0; i < commands.length; ++i) {
        const Clay_RenderCommand *c = &commands.internalArray[i];
        QuadInstance q = {0}; UiTexture texture = 0; UiRenderResult result = UI_RENDER_OK;
        q.box[0] = c->boundingBox.x; q.box[1] = c->boundingBox.y;
        q.box[2] = c->boundingBox.width; q.box[3] = c->boundingBox.height;
        switch (c->commandType) {
        case CLAY_RENDER_COMMAND_TYPE_NONE: continue;
        case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
            color(q.color, c->renderData.rectangle.backgroundColor);
            radii(q.radii, c->renderData.rectangle.cornerRadius); break;
        case CLAY_RENDER_COMMAND_TYPE_BORDER:
            color(q.color, c->renderData.border.color); radii(q.radii, c->renderData.border.cornerRadius);
            q.effect[0] = UI_QUAD_BORDER;
            q.border[0] = c->renderData.border.width.left; q.border[1] = c->renderData.border.width.top;
            q.border[2] = c->renderData.border.width.right; q.border[3] = c->renderData.border.width.bottom;
            break;
        case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            if (!bridge || !bridge->image) { return f->error = UI_RENDER_UNSUPPORTED; }
            result = bridge->image(bridge->context, c->renderData.image.imageData, &texture);
            if (result != UI_RENDER_OK) { return f->error = result; }
            color(q.color, c->renderData.image.backgroundColor);
            if (!q.color[0] && !q.color[1] && !q.color[2] && !q.color[3]) {
                q.color[0] = q.color[1] = q.color[2] = q.color[3] = 1;
            }
            radii(q.radii, c->renderData.image.cornerRadius);
            q.effect[0] = UI_QUAD_IMAGE; q.uv[2] = q.uv[3] = 1; break;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
            result = ui_frame_clip_push(f, (UiRect){q.box[0],q.box[1],q.box[2],q.box[3]},
                c->renderData.clip.horizontal, c->renderData.clip.vertical);
            if (result != UI_RENDER_OK) { return result; } continue;
        case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
            result = ui_frame_clip_pop(f); if (result != UI_RENDER_OK) { return result; } continue;
        default:
            if (!bridge || !bridge->custom) { return f->error = UI_RENDER_UNSUPPORTED; }
            result = bridge->custom(bridge->context, f, c);
            if (result != UI_RENDER_OK) { return f->error = result; } continue;
        }
        result = ui_frame_quad(f, &q, texture);
        if (result != UI_RENDER_OK) { return result; }
    }
    return UI_RENDER_OK;
}
