#include "menu.h"
#include <math.h>

static bool navigate(UiMenu *menu, UiWidgets *ui, UiFocusKey key)
{
    UiFocusItem focus[UI_MENU_ITEMS];
    for (uint32_t i=0;i<menu->count;++i) {
        focus[i]=(UiFocusItem){menu->items[i].id,menu->items[i].disabled};
    }
    return ui_focus_move(ui,focus,menu->count,key,menu->rtl);
}
UiWidgetResult ui_menu_open(UiMenu *menu, UiWidgets *ui, const UiMenuItem *items,
    uint32_t count, bool rtl)
{
    if (!menu || !ui || !items || !count) { return UI_WIDGET_INVALID; }
    if (count>UI_MENU_ITEMS) { return UI_WIDGET_FULL; }
    for (uint32_t i=0;i<count;++i) {
        if (!items[i].id.id || items[i].label.length<0 ||
            (items[i].label.length && !items[i].label.chars) ||
            items[i].icon<UI_ICON_NONE || items[i].icon>=UI_ICON_COUNT) { return UI_WIDGET_INVALID; }
        for (uint32_t j=0;j<i;++j) { if (items[j].id.id==items[i].id.id) { return UI_WIDGET_INVALID; } }
    }
    uint32_t opener=menu->open ? menu->opener : ui->focus;
    *menu=(UiMenu){.items=items,.count=count,.opener=opener,.activated=UI_MENU_NONE,.open=true,.rtl=rtl};
    ui->active=0; (void)navigate(menu,ui,UI_FOCUS_HOME); return UI_WIDGET_OK;
}
void ui_menu_close(UiMenu *menu, UiWidgets *ui)
{
    if (!menu || !ui || !menu->open) { return; }
    menu->open=false; ui->focus=menu->opener; ui->active=0;
}
static bool activate(UiMenu *menu, UiWidgets *ui)
{
    for (uint32_t i=0;i<menu->count;++i) {
        if (!menu->items[i].disabled && ui->focus==menu->items[i].id.id) {
            menu->activated=i; ui_menu_close(menu,ui); return true;
        }
    }
    return false;
}
bool ui_menu_key(UiMenu *menu, UiWidgets *ui, UiMenuKey key)
{
    if (!menu || !ui || !menu->open) { return false; }
    switch (key) {
    case UI_MENU_UP: return navigate(menu,ui,UI_FOCUS_UP);
    case UI_MENU_DOWN: return navigate(menu,ui,UI_FOCUS_DOWN);
    case UI_MENU_HOME: return navigate(menu,ui,UI_FOCUS_HOME);
    case UI_MENU_END: return navigate(menu,ui,UI_FOCUS_END);
    case UI_MENU_ENTER: case UI_MENU_SPACE: return activate(menu,ui);
    case UI_MENU_ESCAPE: ui_menu_close(menu,ui); return true;
    }
    return false;
}
void ui_menu_draw(UiMenu *menu, UiWidgets *ui, Clay_ElementId id, float width)
{
    if (!menu || !ui || !menu->open) { return; }
    if (!isfinite(width) || width<64 || !ui_widgets_declare(ui,id)) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return;
    }
    uint32_t chosen=UI_MENU_NONE;
    CLAY(id,{.layout={.sizing={.width=CLAY_SIZING_FIXED(width)},.padding={4,4,4,4},
        .layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor=ui_theme()->elevated,
        .cornerRadius=CLAY_CORNER_RADIUS(4)}) {
        for (uint32_t i=0;i<menu->count;++i) {
            const UiMenuItem *item=&menu->items[i];
            if (ui_component_row(ui,item->id,item->label,(Clay_String){0},item->icon,
                UI_COMPONENT_MENU,(UiControl){.width=width-8,.height=40,
                    .state=(item->selected ? UI_CONTROL_SELECTED : 0) |
                        (item->disabled ? UI_CONTROL_DISABLED : 0),.rtl=menu->rtl})) { chosen=i; }
        }
    }
    if (chosen!=UI_MENU_NONE) { menu->activated=chosen; ui_menu_close(menu,ui); }
}
bool ui_menu_take_activation(UiMenu *menu, uint32_t *index)
{
    if (!menu || !index || menu->activated==UI_MENU_NONE || !menu->items ||
        menu->activated>=menu->count) { return false; }
    *index=menu->activated; menu->activated=UI_MENU_NONE; return true;
}
