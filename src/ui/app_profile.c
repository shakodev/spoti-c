#include "app.h"
#include "font_roles.h"
#include <math.h>
#include <string.h>

static bool contains(Clay_ElementData element,UiWidgetInput input)
{
    Clay_BoundingBox b=element.boundingBox;
    return element.found && input.x>=b.x && input.x<b.x+b.width && input.y>=b.y && input.y<b.y+b.height;
}
static bool menu_row(App *a,Clay_ElementId id,Clay_String label,float width)
{
    if (!ui_widgets_declare(&a->widgets,id)) { return false; }
    bool hit=ui_widgets_button_event(&a->widgets,id,false);
    uint32_t state=(a->widgets.hover==id.id ? UI_CONTROL_HOVER : 0) |
        (a->widgets.active==id.id && a->widgets.input.down ? UI_CONTROL_PRESSED : 0) |
        (a->widgets.focus==id.id ? UI_CONTROL_FOCUS : 0);
    UiComponentStyle style=ui_component_style(UI_COMPONENT_MENU,state);
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(40)},.padding={12,12,0,0},
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}},.backgroundColor=style.background,.cornerRadius=CLAY_CORNER_RADIUS(style.radius),
        .border={.color=style.outline,.width=CLAY_BORDER_ALL(style.border)},.clip={.horizontal=true,.vertical=true}}) {
        ui_component_label(label,FONT_ROLE_BODY,ui_theme()->small_text,style.foreground,false);
    }
    return hit;
}
UiWidgetInput app_profile_input(App *a,UiWidgetInput input)
{
    if (a->sidebar.profile && input.pressed && !contains(Clay_GetElementData(CLAY_ID("ProfileMenu")),input) &&
        !contains(Clay_GetElementData(CLAY_ID("NavAuth")),input)) {
        a->sidebar.profile=false; a->widgets.focus=CLAY_ID("NavAuth").id;
        a->widgets.active=0; input.pressed=input.released=input.down=input.activate=false; input.cancel=true;
    }
    return input;
}
void app_profile_menu(App *a)
{
    if (!a->sidebar.profile) { return; }
    /* The modal menu owns keyboard traversal while open. Clay's floating hit
     * capture prevents the underlying route from receiving pointer activation. */
    a->control_count=0;
    static const char *const labels[]={"Account","Profile","Recents","Support","Private session","Settings","Log out"};
    static const char *const reasons[]={"Account service is unavailable in this client.","Profile service is unavailable in this client.",
        "Listening history is unavailable in this client.","Support service is unavailable in this client.",
        "Private session is unavailable in this client."};
    float width=fminf(280,a->geometry.width-32);
    CLAY(CLAY_ID("ProfileMenu"),{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIT()},
        .padding=CLAY_PADDING_ALL(4),.layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor={40,40,40,255},
        .cornerRadius=CLAY_CORNER_RADIUS(4),.floating={.attachTo=CLAY_ATTACH_TO_ELEMENT_WITH_ID,.parentId=CLAY_ID("NavAuth").id,
            .attachPoints={CLAY_ATTACH_POINT_RIGHT_TOP,CLAY_ATTACH_POINT_RIGHT_BOTTOM},.offset={0,8},.zIndex=20}}) {
        for (uint32_t i=0;i<7;++i) {
            const char *label=i==6 ? a->model.auth_pending ? "Cancel sign in" :
                a->model.logged_in && a->model.status!=APP_AUTH_EXPIRED ? labels[i] : "Sign in" : labels[i];
            AppAction action={.type=i<5 ? APP_PLAYER_INFO : i==5 ? APP_GO_SETTINGS :
                a->model.auth_pending || (a->model.logged_in && a->model.status!=APP_AUTH_EXPIRED) ? APP_SIGN_OUT : APP_SIGN_IN};
            if (i<5) { strcpy_s(action.entity.title,sizeof(action.entity.title),reasons[i]); }
            Clay_ElementId id=CLAY_IDI("ProfileItem",i);
            if (a->control_count==APP_CONTROLS) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); break; }
            a->controls[a->control_count++]=(AppControl){id,action,false};
            if (menu_row(a,id,app_text(a,(Clay_String){false,(int32_t)strlen(label),label}),width-8)) {
                (void)app_enqueue(a,action);
            }
        }
    }
}
