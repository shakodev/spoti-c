#ifndef SPOTIC_UI_COMPONENTS_H
#define SPOTIC_UI_COMPONENTS_H
#include "widgets.h"
#include "icons.h"
#include "ui_theme.h"

typedef enum UiComponentKind {
    UI_COMPONENT_PRIMARY, UI_COMPONENT_SECONDARY, UI_COMPONENT_TERTIARY,
    UI_COMPONENT_ICON, UI_COMPONENT_CARD, UI_COMPONENT_ROW, UI_COMPONENT_TAB,
    UI_COMPONENT_MENU, UI_COMPONENT_KIND_COUNT
} UiComponentKind;
typedef enum UiControlState {
    UI_CONTROL_NORMAL=0, UI_CONTROL_HOVER=1u, UI_CONTROL_PRESSED=2u,
    UI_CONTROL_SELECTED=4u, UI_CONTROL_DISABLED=8u, UI_CONTROL_FOCUS=16u
} UiControlState;
typedef struct UiControl {
    float width, height;
    uint32_t state; /* Additional presentation state; actual input is combined. */
    bool rtl;
} UiControl;
typedef struct UiComponentStyle {
    Clay_Color background, foreground, outline;
    float radius;
    uint16_t border;
    bool selected;
} UiComponentStyle;
typedef struct UiCardArtwork {
    /* Borrowed texture is center-cropped, never stretched, with a 4-DIP radius.
     * A zero texture draws original caller-colored geometric artwork. */
    UiTexture texture;
    uint32_t source_width, source_height;
    Clay_Color background, foreground;
    uint32_t seed;
} UiCardArtwork;
typedef enum UiStatusKind {
    UI_STATUS_INFORMATION, UI_STATUS_WARNING, UI_STATUS_ERROR,
    UI_STATUS_LOADING, UI_STATUS_EMPTY
} UiStatusKind;
typedef struct UiFocusItem { Clay_ElementId id; bool disabled; } UiFocusItem;
typedef enum UiFocusKey {
    UI_FOCUS_NEXT, UI_FOCUS_PREVIOUS, UI_FOCUS_LEFT, UI_FOCUS_RIGHT,
    UI_FOCUS_UP, UI_FOCUS_DOWN, UI_FOCUS_HOME, UI_FOCUS_END
} UiFocusKey;

UiComponentStyle ui_component_style(UiComponentKind kind, uint32_t state);
/* Caller-owned labels, no retained pointers. IDs must be stable and unique.
 * width/height are DIP. Disabled controls never capture/activate.
 * Extra state bits support deterministic state capture without OS input.
 * Labels are clipped to control bounds. Cards require height>=width+48. */
bool ui_component_button(UiWidgets *ui, Clay_ElementId id, Clay_String label,
    UiIcon icon, UiComponentKind kind, UiControl control);
bool ui_component_row(UiWidgets *ui, Clay_ElementId id, Clay_String label,
    Clay_String detail, UiIcon icon, UiComponentKind kind, UiControl control);
bool ui_component_card(UiWidgets *ui, Clay_ElementId id, Clay_String title,
    Clay_String subtitle, UiCardArtwork artwork, UiControl control);
void ui_component_status(UiWidgets *ui, Clay_ElementId id, Clay_String label,
    UiStatusKind kind, float width, bool rtl);
void ui_component_label(Clay_String label, uint16_t font, uint16_t size,
    Clay_Color color, bool rtl);
/* Tab/Shift+Tab use NEXT/PREVIOUS. Arrows mirror only for RTL. Wraps and
 * skips disabled items; all-disabled groups leave focus unchanged. */
bool ui_focus_move(UiWidgets *ui, const UiFocusItem *items, uint32_t count,
    UiFocusKey key, bool rtl);
#endif
