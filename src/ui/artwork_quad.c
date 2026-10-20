#include "artwork_quad.h"
#include <math.h>

const uint32_t ui_artwork_command_tag=UI_ARTWORK_COMMAND_MAGIC;
static UiRenderResult invalid(UiRenderFrame *frame)
{
    if (!frame) { return UI_RENDER_INVALID; }
    if (frame->error==UI_RENDER_OK) { frame->error=UI_RENDER_INVALID; }
    return frame->error;
}
UiRenderResult ui_artwork_draw(UiRenderFrame *frame, UiTexture texture,
    uint32_t source_width, uint32_t source_height, UiRect bounds, float radius)
{
    if (!frame || frame->error!=UI_RENDER_OK) { return frame ? frame->error : UI_RENDER_INVALID; }
    if (!texture || !source_width || !source_height || source_width>UI_MAX_SURFACE ||
        source_height>UI_MAX_SURFACE || !isfinite(bounds.x) || !isfinite(bounds.y) ||
        !isfinite(bounds.w) || !isfinite(bounds.h) || bounds.w<=0 || bounds.h<=0 ||
        bounds.w>1000000 || bounds.h>1000000 || !isfinite(radius) || radius<0 ||
        radius>fminf(bounds.w,bounds.h)*0.5f) { return invalid(frame); }
    float source=(float)source_width/(float)source_height, target=bounds.w/bounds.h;
    float uw=source>target ? target/source : 1;
    float uh=source<target ? source/target : 1;
    QuadInstance q={.box={bounds.x,bounds.y,bounds.w,bounds.h},.color={1,1,1,1},
        .uv={(1-uw)*0.5f,(1-uh)*0.5f,(1+uw)*0.5f,(1+uh)*0.5f},
        .radii={radius,radius,radius,radius},.effect={UI_QUAD_IMAGE,0,0,0}};
    return ui_frame_quad(frame,&q,texture);
}
UiRenderResult ui_artwork_dispatch(UiRenderFrame *frame,
    const Clay_RenderCommand *command, bool *handled)
{
    if (!handled || !command) { return invalid(frame); }
    *handled=false;
    if (command->commandType!=CLAY_RENDER_COMMAND_TYPE_CUSTOM ||
        command->userData!=&ui_artwork_command_tag) { return UI_RENDER_OK; }
    *handled=true;
    const UiArtworkCommand *image=command->renderData.custom.customData;
    if (!image || image->magic!=UI_ARTWORK_COMMAND_MAGIC ||
        !isfinite(image->opacity) || image->opacity<0 || image->opacity>1) { return invalid(frame); }
    Clay_BoundingBox b=command->boundingBox;
    uint32_t before=frame ? frame->quad_count : 0;
    UiRenderResult result=ui_artwork_draw(frame,image->texture,image->source_width,
        image->source_height,(UiRect){b.x,b.y,b.width,b.height},image->radius);
    if (result==UI_RENDER_OK && frame->quad_count>before) { frame->quads[before].color[3]=image->opacity; }
    return result;
}
