#include "app.h"
#include <math.h>

bool app_shell_button(App *a, Clay_ElementId id, Clay_String text, UiIcon icon,
    UiComponentKind kind, float width, float height, int type, bool selected, bool disabled)
{
    if (a->control_count==APP_CONTROLS) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return false; }
    AppAction action={.type=(AppActionType)type};
    a->controls[a->control_count++]=(AppControl){id,action,disabled};
    bool hit=ui_component_button(&a->widgets,id,app_text(a,text),icon,kind,
        (UiControl){.width=width,.height=height,.state=(selected ? UI_CONTROL_SELECTED : 0)|(disabled ? UI_CONTROL_DISABLED : 0)});
    if (hit) { (void)app_enqueue(a,action); }
    return hit;
}
void app_header(App *a)
{
    float group=fminf(602,a->geometry.width-200), side=(a->geometry.width-16-group)/2;
    CLAY(CLAY_ID("AppTitle"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(56)},
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(side),CLAY_SIZING_FIXED(48)},.childGap=4,
            .childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
            (void)app_shell_button(a,CLAY_ID("NavBack"),CLAY_STRING(""),UI_ICON_CHEVRON_LEFT,UI_COMPONENT_ICON,40,40,
                APP_BACK,false,!a->model.history_count);
            (void)app_shell_button(a,CLAY_ID("NavForward"),CLAY_STRING(""),UI_ICON_CHEVRON_RIGHT,UI_COMPONENT_ICON,40,40,
                APP_BACK,false,true);
        }
        CLAY(CLAY_ID("HeaderCenter"),{.layout={.sizing={CLAY_SIZING_FIXED(group),CLAY_SIZING_FIXED(48)},.childGap=8}}) {
            (void)app_shell_button(a,CLAY_ID("NavHome"),CLAY_STRING(""),UI_ICON_HOME,UI_COMPONENT_ICON,48,48,
                APP_GO_HOME,a->model.location.screen==APP_HOME,false);
            (void)app_shell_button(a,CLAY_ID("NavSearch"),a->services.demo ? CLAY_STRING("Search | Offline demo") : CLAY_STRING("Search"),
                UI_ICON_SEARCH,UI_COMPONENT_SECONDARY,group-56,48,APP_GO_SEARCH,
                a->model.location.screen==APP_SEARCH,false);
        }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(side),CLAY_SIZING_FIXED(48)},.childGap=4,
            .childAlignment={CLAY_ALIGN_X_RIGHT,CLAY_ALIGN_Y_CENTER}}}) {
            (void)app_shell_button(a,CLAY_ID("NavSettings"),CLAY_STRING(""),UI_ICON_MORE,UI_COMPONENT_ICON,36,40,
                APP_GO_SETTINGS,a->model.location.screen==APP_SETTINGS,false);
            (void)app_shell_button(a,CLAY_ID("NavAuth"),CLAY_STRING(""),UI_ICON_ACCOUNT,UI_COMPONENT_ICON,48,48,
                APP_PROFILE_TOGGLE,a->sidebar.profile,false);
        }
    }
}
