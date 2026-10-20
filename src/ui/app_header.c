#include "app.h"
#include "font_roles.h"
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
static bool header_control(App *a,Clay_ElementId id,AppActionType type)
{
    if (a->control_count==APP_CONTROLS || !ui_widgets_declare(&a->widgets,id)) {
        ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return false;
    }
    AppAction action={.type=type}; a->controls[a->control_count++]=(AppControl){id,action,false};
    if (ui_widgets_button_event(&a->widgets,id,false)) { (void)app_enqueue(a,action); }
    return true;
}
static Clay_Color header_fill(const App *a,Clay_ElementId id)
{
    const UiTheme *theme=ui_theme();
    return a->widgets.active==id.id && a->widgets.input.down ? theme->pressed :
        a->widgets.hover==id.id ? theme->hover : theme->elevated;
}
static void home(App *a)
{
    Clay_ElementId id=CLAY_ID("NavHome");
    if (!header_control(a,id,APP_GO_HOME)) { return; }
    const UiTheme *theme=ui_theme(); bool focus=a->widgets.focus==id.id;
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(48),CLAY_SIZING_FIXED(48)},
        .childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},.backgroundColor=header_fill(a,id),
        .cornerRadius=CLAY_CORNER_RADIUS(24),.border={.color=theme->focus,.width=CLAY_BORDER_ALL(focus ? theme->focus_width : 0)}}) {
        ui_icon(&a->widgets,CLAY_ID("NavHomeIcon"),UI_ICON_HOME,24,
            a->model.location.screen==APP_HOME || a->widgets.hover==id.id ? theme->text : theme->subdued,false);
    }
}
static void search(App *a,float width)
{
    Clay_ElementId id=CLAY_ID("NavSearch");
    if (!header_control(a,id,APP_GO_SEARCH)) { return; }
    const UiTheme *theme=ui_theme(); bool focus=a->widgets.focus==id.id;
    Clay_String placeholder=a->services.demo ? width<240 ? CLAY_STRING("Offline demo") :
        CLAY_STRING("What do you want to play? | Offline demo") : CLAY_STRING("What do you want to play?");
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(48)},.padding={12,12,0,0},.childGap=12,
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}},.backgroundColor=header_fill(a,id),.cornerRadius=CLAY_CORNER_RADIUS(24),
        .border={.color=theme->focus,.width=CLAY_BORDER_ALL(focus ? theme->focus_width : 0)},.clip={.horizontal=true,.vertical=true}}) {
        ui_icon(&a->widgets,CLAY_ID("NavSearchIcon"),UI_ICON_SEARCH,24,theme->subdued,false);
        CLAY(CLAY_ID("NavSearchLabel"),{.layout={.sizing={CLAY_SIZING_GROW(0),CLAY_SIZING_FIT()}},.clip={.horizontal=true}}) {
            ui_component_label(placeholder,FONT_ROLE_BODY,theme->body,theme->subdued,false);
        }
    }
}
void app_header(App *a)
{
    float group=fminf(602,a->geometry.width-200), side=(a->geometry.width-16-group)/2;
    CLAY(CLAY_ID("AppTitle"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(64)},
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(side),CLAY_SIZING_FIXED(48)},.childGap=4,
            .childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
            (void)app_shell_button(a,CLAY_ID("NavBack"),CLAY_STRING(""),UI_ICON_CHEVRON_LEFT,UI_COMPONENT_ICON,40,40,
                APP_BACK,false,!a->model.history_count);
            (void)app_shell_button(a,CLAY_ID("NavForward"),CLAY_STRING(""),UI_ICON_CHEVRON_RIGHT,UI_COMPONENT_ICON,40,40,
                APP_BACK,false,true);
        }
        CLAY(CLAY_ID("HeaderCenter"),{.layout={.sizing={CLAY_SIZING_FIXED(group),CLAY_SIZING_FIXED(48)},.childGap=8}}) {
            home(a); search(a,group-56);
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
