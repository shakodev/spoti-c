#include "icons.h"
#include <math.h>

static const char icon_tag=0;
typedef struct Glyph { UiIcon icon; Clay_Color color; } Glyph;
void ui_icon(UiWidgets *ui, UiIcon icon, float size, Clay_Color color)
{
    if (icon<=UI_ICON_NONE || icon>=UI_ICON_COUNT || !isfinite(size) || size<=0) { return; }
    /* Caller-owned frame scratch survives EndLayout through submission. A
     * Clay background would emit an unwanted rectangle behind the glyph. */
    Glyph *glyph=ui_widgets_scratch(ui,sizeof(*glyph),_Alignof(Glyph));
    if (!glyph) { return; } *glyph=(Glyph){icon,color};
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(size),CLAY_SIZING_FIXED(size)}},
        .custom={.customData=glyph},.userData=(void *)&icon_tag}) { }
}
bool ui_icon_command(const Clay_RenderCommand *c)
{
    return c && c->commandType==CLAY_RENDER_COMMAND_TYPE_CUSTOM && c->userData==&icon_tag;
}
UiRenderResult ui_icon_render(UiRenderFrame *f, const Clay_RenderCommand *c)
{
    if (!f || !ui_icon_command(c) || !c->renderData.custom.customData) { return UI_RENDER_INVALID; }
    const Glyph *glyph=c->renderData.custom.customData; UiIcon icon=glyph->icon;
    if (icon<=UI_ICON_NONE || icon>=UI_ICON_COUNT) { return UI_RENDER_INVALID; }
    Clay_Color color=glyph->color;
    QuadInstance q={.box={c->boundingBox.x,c->boundingBox.y,c->boundingBox.width,c->boundingBox.height},
        .color={color.r/255,color.g/255,color.b/255,color.a/255},.effect={UI_QUAD_ICON,(float)icon}};
    return ui_frame_quad(f,&q,0);
}
