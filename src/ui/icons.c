#include "icons.h"
#include <string.h>
UiRenderResult ui_icon_draw(UiRenderFrame *frame, UiRect box, UiIcon icon, const float color[4])
{
    if (!frame || !color || icon<UI_ICON_PLAY || icon>UI_ICON_FULLSCREEN) { return UI_RENDER_INVALID; }
    QuadInstance quad={.box={box.x,box.y,box.w,box.h},.effect={UI_QUAD_ICON,(float)icon}};
    memcpy(quad.color,color,sizeof(quad.color)); return ui_frame_quad(frame,&quad,0);
}
