#ifndef SPOTIC_UI_MENU_H
#define SPOTIC_UI_MENU_H
#include "components.h"
#define UI_MENU_ITEMS 16u
#define UI_MENU_NONE UINT32_MAX
typedef struct UiMenuItem {
    Clay_ElementId id;
    Clay_String label;
    UiIcon icon;
    bool disabled, selected;
} UiMenuItem;
typedef struct UiMenu {
    /* Borrowed immutable item/label storage until close. */
    const UiMenuItem *items;
    uint32_t count, opener, activated;
    bool open, rtl;
} UiMenu;
typedef enum UiMenuKey {
    UI_MENU_UP, UI_MENU_DOWN, UI_MENU_HOME, UI_MENU_END,
    UI_MENU_ENTER, UI_MENU_SPACE, UI_MENU_ESCAPE
} UiMenuKey;
UiWidgetResult ui_menu_open(UiMenu *menu, UiWidgets *ui, const UiMenuItem *items,
    uint32_t count, bool rtl);
void ui_menu_close(UiMenu *menu, UiWidgets *ui);
bool ui_menu_key(UiMenu *menu, UiWidgets *ui, UiMenuKey key);
/* Places menu in current Clay container. Caller positions popup anchor.
 * Activating with pointer or keyboard closes and restores opener focus. */
void ui_menu_draw(UiMenu *menu, UiWidgets *ui, Clay_ElementId id, float width);
bool ui_menu_take_activation(UiMenu *menu, uint32_t *index);
#endif
