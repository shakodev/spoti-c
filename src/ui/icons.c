#include "icons_internal.h"
#include <math.h>
#include <string.h>

const uint32_t ui_icon_command_tag = UI_ICON_COMMAND_MAGIC;

static UiRenderResult invalid(UiRenderFrame *frame)
{
    if (!frame) { return UI_RENDER_INVALID; }
    if (frame->error == UI_RENDER_OK) { frame->error = UI_RENDER_INVALID; }
    return frame->error;
}

UiRenderResult ui_icon_draw(UiRenderFrame *frame, UiIcon icon, UiRect bounds,
    const float color[4], bool rtl)
{
    if (!frame || frame->error != UI_RENDER_OK) { return frame ? frame->error : UI_RENDER_INVALID; }
    if ((unsigned)icon >= UI_ICON_COUNT || !color || !frame->clip_count ||
        !isfinite(bounds.x) || !isfinite(bounds.y) || !isfinite(bounds.w) || !isfinite(bounds.h) ||
        fabsf(bounds.x) > 1000000 || fabsf(bounds.y) > 1000000 ||
        bounds.w <= 0 || bounds.h <= 0 || bounds.w > 1000000 || bounds.h > 1000000) {
        return invalid(frame);
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (!isfinite(color[i]) || color[i] < 0 || color[i] > 1) { return invalid(frame); }
    }
    if (icon == UI_ICON_NONE) { return UI_RENDER_OK; }
    float size = fminf(bounds.w,bounds.h);
    QuadInstance q = {.box={bounds.x + (bounds.w-size)*0.5f,
        bounds.y + (bounds.h-size)*0.5f,size,size},
        .effect={UI_QUAD_ICON,(float)icon,0,0}};
    q.effect[2] = rtl && (icon == UI_ICON_CHEVRON_LEFT || icon == UI_ICON_CHEVRON_RIGHT) ? 1.0f : 0.0f;
    memcpy(q.color,color,sizeof(q.color));
    return ui_frame_quad(frame,&q,0);
}

UiRenderResult ui_icon_dispatch(UiRenderFrame *frame,
    const Clay_RenderCommand *command, bool *handled)
{
    if (!handled) { return invalid(frame); }
    *handled = false;
    if (!command) { return invalid(frame); }
    if (command->commandType != CLAY_RENDER_COMMAND_TYPE_CUSTOM ||
        command->userData != &ui_icon_command_tag) { return UI_RENDER_OK; }
    *handled = true;
    const UiIconCommand *icon = command->renderData.custom.customData;
    if (!icon || icon->magic != UI_ICON_COMMAND_MAGIC) { return invalid(frame); }
    float color[4] = {icon->color.r/255,icon->color.g/255,icon->color.b/255,icon->color.a/255};
    Clay_BoundingBox b = command->boundingBox;
    return ui_icon_draw(frame,icon->icon,(UiRect){b.x,b.y,b.width,b.height},color,icon->rtl);
}
