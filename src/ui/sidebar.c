#include "app.h"

void app_sidebar(App *a)
{
    float width = a->geometry.sidebar; bool collapsed=width<=64;
    CLAY(CLAY_ID("AppSidebar"),{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_GROW()},
        .padding=CLAY_PADDING_ALL(8),.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
        .backgroundColor={18,18,18,255},.cornerRadius=CLAY_CORNER_RADIUS(8)}) {
        (void)app_shell_button(a,CLAY_ID("NavLibrary"),collapsed ? CLAY_STRING("") : CLAY_STRING("Your Library"),
            UI_ICON_LIBRARY,UI_COMPONENT_TERTIARY,width-16,48,APP_GO_LIBRARY,a->model.location.screen==APP_LIBRARY,false);
        (void)app_shell_button(a,CLAY_ID("SidebarCollapse"),collapsed ? CLAY_STRING("") : CLAY_STRING("Collapse Library"),
            collapsed ? UI_ICON_CHEVRON_RIGHT : UI_ICON_CHEVRON_LEFT,UI_COMPONENT_TERTIARY,width-16,32,
            APP_SIDEBAR_TOGGLE,false,a->geometry.compact);
        if (!collapsed) {
            app_label(CLAY_STRING("Your saved music"),14,(Clay_Color){179,179,179,255});
            app_label(CLAY_STRING("Open Library to browse"),12,(Clay_Color){179,179,179,255});
        }
    }
}
