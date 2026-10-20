#include "component_showcase.h"
#include "font_roles.h"
#include "menu.h"

static const uint32_t states[UI_SHOWCASE_STATES]={
    UI_CONTROL_NORMAL,UI_CONTROL_HOVER,UI_CONTROL_PRESSED,
    UI_CONTROL_SELECTED,UI_CONTROL_DISABLED,UI_CONTROL_FOCUS
};
static const Clay_String names[UI_SHOWCASE_STATES]={
    {true,6,"Normal"},{true,5,"Hover"},{true,7,"Pressed"},
    {true,8,"Selected"},{true,8,"Disabled"},{true,5,"Focus"}
};
static void state_column(UiWidgets *ui, uint32_t index, bool rtl)
{
    UiControl c={.width=156,.height=48,.state=states[index],.rtl=rtl};
    CLAY(CLAY_IDI("showcase-column",index),{.layout={.sizing={.width=CLAY_SIZING_FIXED(156)},
        .childGap=12,.layoutDirection=CLAY_TOP_TO_BOTTOM}}) {
        ui_component_label(names[index],FONT_ROLE_BOLD,14,ui_theme()->subdued,rtl);
        (void)ui_component_button(ui,CLAY_IDI("showcase-primary",index),CLAY_STRING("Play"),
            UI_ICON_PLAY,UI_COMPONENT_PRIMARY,c);
        (void)ui_component_button(ui,CLAY_IDI("showcase-secondary",index),CLAY_STRING("Follow"),
            UI_ICON_NONE,UI_COMPONENT_SECONDARY,c);
        (void)ui_component_button(ui,CLAY_IDI("showcase-tertiary",index),CLAY_STRING("Show all"),
            UI_ICON_NONE,UI_COMPONENT_TERTIARY,c);
        UiControl icon=c; icon.width=48;
        (void)ui_component_button(ui,CLAY_IDI("showcase-icon",index),(Clay_String){0},
            UI_ICON_MORE,UI_COMPONENT_ICON,icon);
        UiControl card=c; card.height=216;
        (void)ui_component_card(ui,CLAY_IDI("showcase-card",index),CLAY_STRING("Evening light"),
            CLAY_STRING("Original artwork"),(UiCardArtwork){.background={51,41,88,255},
                .foreground={197,148,235,255},.seed=1},card);
        UiControl row=c; row.height=64;
        (void)ui_component_row(ui,CLAY_IDI("showcase-row",index),CLAY_STRING("Your library"),
            CLAY_STRING("Saved music"),UI_ICON_LIBRARY,UI_COMPONENT_ROW,row);
        c.height=32;
        (void)ui_component_button(ui,CLAY_IDI("showcase-tab",index),CLAY_STRING("Music"),
            UI_ICON_NONE,UI_COMPONENT_TAB,c);
    }
}
void ui_component_showcase(UiWidgets *ui, bool rtl)
{
    if (!ui || !ui->in_frame) { return; }
    const UiTheme *t=ui_theme();
    CLAY(CLAY_ID("showcase"),{.layout={.sizing={CLAY_SIZING_FIXED(UI_SHOWCASE_WIDTH),CLAY_SIZING_FIXED(UI_SHOWCASE_HEIGHT)},
        .padding={12,12,12,12},.childGap=16,.layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor=t->panel}) {
        ui_component_label(CLAY_STRING("Shared desktop components"),FONT_ROLE_TITLE,32,t->text,rtl);
        ui_component_label(CLAY_STRING("DIP geometry  /  Segoe UI fallback  /  Original geometric icons"),
            FONT_ROLE_BODY,14,t->subdued,rtl);
        CLAY(CLAY_ID("showcase-states"),{.layout={.childGap=16}}) {
            for (uint32_t j=0;j<UI_SHOWCASE_STATES;++j) {
                state_column(ui,rtl ? UI_SHOWCASE_STATES-1-j : j,rtl);
            }
        }
        CLAY(CLAY_ID("showcase-bottom"),{.layout={.childGap=16}}) {
            CLAY(CLAY_ID("showcase-menu"),{.layout={.sizing={.width=CLAY_SIZING_FIXED(328)},
                .padding={4,4,4,4},.layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor=t->elevated,
                .cornerRadius=CLAY_CORNER_RADIUS(4)}) {
                (void)ui_component_row(ui,CLAY_ID("showcase-menu-play"),CLAY_STRING("Play next"),
                    (Clay_String){0},UI_ICON_PLAY,UI_COMPONENT_MENU,
                    (UiControl){.width=320,.height=40,.state=UI_CONTROL_FOCUS,.rtl=rtl});
                (void)ui_component_row(ui,CLAY_ID("showcase-menu-save"),CLAY_STRING("Saved to your library"),
                    (Clay_String){0},UI_ICON_CHECK,UI_COMPONENT_MENU,
                    (UiControl){.width=320,.height=40,.state=UI_CONTROL_SELECTED,.rtl=rtl});
                (void)ui_component_row(ui,CLAY_ID("showcase-menu-disabled"),CLAY_STRING("Unavailable"),
                    (Clay_String){0},UI_ICON_PLUS,UI_COMPONENT_MENU,
                    (UiControl){.width=320,.height=40,.state=UI_CONTROL_DISABLED,.rtl=rtl});
            }
            CLAY(CLAY_ID("showcase-statuses"),{.layout={.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM}}) {
                ui_component_status(ui,CLAY_ID("showcase-info"),CLAY_STRING("Your library is ready"),
                    UI_STATUS_INFORMATION,672,rtl);
                ui_component_status(ui,CLAY_ID("showcase-warning"),CLAY_STRING("Offline: some music is unavailable"),
                    UI_STATUS_WARNING,672,rtl);
                ui_component_label(CLAY_STRING("RTL: \xd8\xa7\xd9\x84\xd9\x85\xd9\x88\xd8\xb3\xd9\x8a\xd9\x82\xd9\x89  /  \xd7\x9e\xd7\x95\xd7\x96\xd7\x99\xd7\xa7\xd7\x94"),
                    FONT_ROLE_BODY,16,t->text,true);
            }
        }
    }
}
