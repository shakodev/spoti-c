#ifndef SPOTIC_UI_ICONS_H
#define SPOTIC_UI_ICONS_H
#include "renderer.h"
#include "widgets.h"

/* Values are shared with ui.hlsl; icons use original 24-unit geometry. */
typedef enum UiIcon {
    UI_ICON_NONE, UI_ICON_HOME, UI_ICON_SEARCH, UI_ICON_LIBRARY,
    UI_ICON_PLAY, UI_ICON_PAUSE, UI_ICON_PREVIOUS, UI_ICON_NEXT,
    UI_ICON_VOLUME, UI_ICON_MUTE, UI_ICON_MORE, UI_ICON_CHECK,
    UI_ICON_CLOSE, UI_ICON_PLUS, UI_ICON_CHEVRON_LEFT,
    UI_ICON_CHEVRON_RIGHT, UI_ICON_INFO, UI_ICON_WARNING, UI_ICON_COUNT
} UiIcon;

/* One untextured quad, aspect-fitted and centered in bounds. Geometry remains
 * in DIP; the renderer scales once. Only navigation chevrons mirror in RTL.
 * NONE is a validated no-op. Errors are sticky until ui_frame_begin. */
UiRenderResult ui_icon_draw(UiRenderFrame *frame, UiIcon icon, UiRect bounds,
    const float color[4], bool rtl);
/* Descriptor storage comes from the bounded frame scratch arena and stays
 * valid until the next ui_widgets_begin; consume the resulting commands first. */
void ui_icon(UiWidgets *ui, Clay_ElementId id, UiIcon icon, float size,
    Clay_Color color, bool rtl);

#endif
