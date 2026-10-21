#include "components.h"
#include <math.h>

UiComponentColors ui_component_colors(UiButtonStyle s, bool disabled, bool hover, bool pressed, bool focus)
{
    UiComponentColors c={.text=UI_TEXT,.border=UI_TEXT,.border_width=focus && !disabled ? 2 : 0};
    bool flat=s.kind==UI_BUTTON_ROW || s.kind==UI_BUTTON_QUIET || s.kind==UI_BUTTON_MENU;
    c.background=flat ? (Clay_Color){0} : UI_HIGHLIGHT;
    if (s.kind==UI_BUTTON_CARD) { c.background=UI_PANEL; }
    if (s.selected) { c.background=s.kind==UI_BUTTON_TAB ? UI_TEXT : UI_ELEVATED_HOVER; }
    if (s.kind==UI_BUTTON_TAB && s.selected) { c.text=UI_CANVAS; }
    if (s.current) { c.text=UI_ACCENT; }
    if (s.kind==UI_BUTTON_PRIMARY) { c.background=UI_ACCENT; c.text=UI_CANVAS; }
    if (!disabled && hover) {
        c.background=s.kind==UI_BUTTON_PRIMARY ? UI_ACCENT_HOVER :
            s.kind==UI_BUTTON_TAB && s.selected ? UI_TEXT : UI_ELEVATED_HOVER;
    }
    if (!disabled && pressed) {
        c.background=s.kind==UI_BUTTON_PRIMARY ? (Clay_Color){22,170,75,255} : UI_HIGHLIGHT;
    }
    if (disabled) { c.text=UI_DISABLED; c.background=flat ? (Clay_Color){0} : UI_HIGHLIGHT; }
    return c;
}
bool ui_component_button(UiWidgets *ui, Clay_ElementId id, Clay_String text, float width,
    float height, bool disabled, UiButtonStyle style)
{
    if (!ui || !ui->in_frame) { return false; }
    if (!isfinite(width) || !isfinite(height) || width<=0 || height<=0 ||
        text.length<0 || (text.length && !text.chars) || style.kind<UI_BUTTON_SECONDARY || style.kind>UI_BUTTON_MENU ||
        style.icon<UI_ICON_NONE || style.icon>=UI_ICON_COUNT) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return false;
    }
    UiControlState state=ui_control_state(ui,id,disabled);
    if (ui->error!=UI_WIDGET_OK) { return false; }
    UiComponentColors c=ui_component_colors(style,disabled,state.hover,state.pressed,state.focus);
    bool row=style.kind==UI_BUTTON_ROW || style.kind==UI_BUTTON_MENU || style.kind==UI_BUTTON_CARD;
    float radius=row ? 4 : height/2;
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(height)},.childGap=8,
        .padding={row ? 4 : 0,row ? 4 : 0,0,0},
        .childAlignment={row ? CLAY_ALIGN_X_LEFT : CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},
        .backgroundColor=c.background,.cornerRadius=CLAY_CORNER_RADIUS(radius),
        .border={.color=c.border,.width=CLAY_BORDER_ALL(c.border_width)},.clip={.horizontal=true,.vertical=true}}) {
        if (style.icon!=UI_ICON_NONE) { ui_icon(ui,style.icon,fminf(24,height-8),c.text); }
        if (text.length) {
            CLAY_TEXT(text,CLAY_TEXT_CONFIG({.fontId=row ? 0 : 3,.fontSize=style.kind==UI_BUTTON_TAB ? 14 : 16,
                .textColor=c.text,.wrapMode=CLAY_TEXT_WRAP_NONE}));
        }
    }
    return state.activated;
}
bool ui_component_card(UiWidgets *ui, Clay_ElementId id, UiCardContent card, float width, float cover, bool disabled, bool selected)
{
    if (!ui || !ui->in_frame) { return false; }
    if (!isfinite(width) || !isfinite(cover) || cover<24 || width<cover+16 ||
        card.title.length<0 || (card.title.length && !card.title.chars) ||
        card.subtitle.length<0 || (card.subtitle.length && !card.subtitle.chars)) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return false;
    }
    UiControlState state=ui_control_state(ui,id,disabled);
    UiComponentColors c=ui_component_colors((UiButtonStyle){.kind=UI_BUTTON_CARD,.selected=selected},
        disabled,state.hover,state.pressed,state.focus);
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(cover+76)},.padding=CLAY_PADDING_ALL(8),
        .childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor=c.background,
        .cornerRadius=CLAY_CORNER_RADIUS(UI_CARD_RADIUS),.border={.color=c.border,.width=CLAY_BORDER_ALL(c.border_width)},
        .clip={.horizontal=true,.vertical=true}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(cover),CLAY_SIZING_FIXED(cover)}},
            .backgroundColor=UI_HIGHLIGHT,.cornerRadius=CLAY_CORNER_RADIUS(card.round ? cover/2 : 4),
            .custom={.customData=card.artwork}}) { }
        CLAY_TEXT(card.title,CLAY_TEXT_CONFIG({.fontId=3,.fontSize=16,.textColor=c.text,.wrapMode=CLAY_TEXT_WRAP_NONE}));
        CLAY_TEXT(card.subtitle,CLAY_TEXT_CONFIG({.fontSize=13,.textColor=UI_SUBDUED,.wrapMode=CLAY_TEXT_WRAP_NONE}));
    }
    return state.activated;
}
