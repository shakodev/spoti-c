#ifndef SPOTIC_UI_DETAIL_COMPONENTS_H
#define SPOTIC_UI_DETAIL_COMPONENTS_H
#include "widgets.h"
#include "design_tokens.h"
#include "icons.h"
typedef enum UiButtonKind { UI_BUTTON_SECONDARY, UI_BUTTON_PRIMARY,
    UI_BUTTON_QUIET, UI_BUTTON_TAB, UI_BUTTON_ROW, UI_BUTTON_CARD, UI_BUTTON_MENU } UiButtonKind;
typedef struct UiButtonStyle { UiButtonKind kind; UiIcon icon; bool selected, current; } UiButtonStyle;
typedef struct UiComponentColors { Clay_Color background, text, border; uint16_t border_width; } UiComponentColors;
UiComponentColors ui_component_colors(UiButtonStyle, bool disabled, bool hover, bool pressed, bool focus);
bool ui_detail_button(UiWidgets *, Clay_ElementId, Clay_String, float width,
    float height, bool disabled, UiButtonStyle);
typedef struct UiCardContent { Clay_String title, subtitle; void *artwork; bool round; } UiCardContent;
bool ui_detail_card(UiWidgets *, Clay_ElementId, UiCardContent, float width, float cover, bool disabled, bool selected);
void ui_detail_icon(UiWidgets *, UiIcon, float, Clay_Color);
bool ui_icon_command(const Clay_RenderCommand *);
UiRenderResult ui_icon_render(UiRenderFrame *, const Clay_RenderCommand *);
#endif
