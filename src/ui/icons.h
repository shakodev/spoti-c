#ifndef SPOTIC_UI_ICONS_H
#define SPOTIC_UI_ICONS_H
#include "renderer_clay.h"
#include "widgets.h"
/* Independently authored geometry; numeric values are the HLSL ABI. */
typedef enum UiIcon { UI_ICON_NONE, UI_ICON_PLAY, UI_ICON_PAUSE, UI_ICON_BACK,
    UI_ICON_FORWARD, UI_ICON_SEARCH, UI_ICON_MORE, UI_ICON_CLOCK, UI_ICON_CLOSE,
    UI_ICON_CHECK, UI_ICON_PLUS, UI_ICON_MUSIC, UI_ICON_COUNT } UiIcon;
void ui_icon(UiWidgets *, UiIcon icon, float size, Clay_Color color);
bool ui_icon_command(const Clay_RenderCommand *command);
UiRenderResult ui_icon_render(UiRenderFrame *, const Clay_RenderCommand *);
#endif
