#include "app.h"

void app_sidebar(App *a)
{
    float width = a->geometry.sidebar;
    bool login = !a->model.logged_in || a->model.status == APP_AUTH_EXPIRED;
    CLAY(CLAY_ID("AppSidebar"),{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_GROW()},
        .padding={4,4,8,8},.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
        .backgroundColor={15,15,15,255},.cornerRadius=CLAY_CORNER_RADIUS(8)}) {
        (void)app_button(a,CLAY_ID("NavHome"),CLAY_STRING("Home"),width-8,(AppAction){.type=APP_GO_HOME},false);
        (void)app_button(a,CLAY_ID("NavLibrary"),CLAY_STRING("Library"),width-8,(AppAction){.type=APP_GO_LIBRARY},false);
        (void)app_button(a,CLAY_ID("NavSettings"),a->geometry.compact ? CLAY_STRING("Prefs") : CLAY_STRING("Settings"),
            width-8,(AppAction){.type=APP_GO_SETTINGS},false);
        (void)app_button(a,CLAY_ID("NavAuth"),a->model.auth_pending ? CLAY_STRING("Cancel") : login ? CLAY_STRING("Sign in") : CLAY_STRING("Log out"),
            width-8,(AppAction){.type=a->model.auth_pending || !login ? APP_SIGN_OUT : APP_SIGN_IN},false);
    }
}
