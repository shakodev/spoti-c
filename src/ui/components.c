#include "components.h"
#include "font_roles.h"
#include "artwork_quad.h"
#include <math.h>

static Clay_ElementId child(Clay_ElementId id, uint32_t index)
{
    return Clay__HashStringWithOffset(CLAY_STRING("component-child"),index,id.id);
}
static bool valid(UiWidgets *ui, Clay_ElementId id, UiControl c)
{
    if (!isfinite(c.width) || !isfinite(c.height) || c.width<=0 || c.height<=0 ||
        (c.state & ~31u)) { ui_widgets_fail(ui,UI_WIDGET_INVALID); return false; }
    return ui_widgets_declare(ui,id);
}
static bool label_valid(Clay_String label)
{
    return label.length>=0 && (!label.length || label.chars);
}
UiComponentStyle ui_component_style(UiComponentKind kind, uint32_t state)
{
    const UiTheme *t=ui_theme();
    UiComponentStyle s={.foreground=t->text,.radius=9999,.selected=(state&UI_CONTROL_SELECTED)!=0};
    bool hover=(state&UI_CONTROL_HOVER)!=0, press=(state&UI_CONTROL_PRESSED)!=0;
    bool disabled=(state&UI_CONTROL_DISABLED)!=0;
    if (disabled) { hover=false; press=false; }
    if (kind==UI_COMPONENT_PRIMARY) {
        s.background=press ? t->accent_pressed : hover ? t->accent_hover : t->accent;
        s.foreground=t->on_accent;
    } else if (kind==UI_COMPONENT_SECONDARY) {
        s.background=press ? t->tinted_pressed : hover ? t->tinted_hover : (Clay_Color){0};
        s.outline=hover ? t->text : t->outline; s.border=1;
    } else if (kind==UI_COMPONENT_CARD || kind==UI_COMPONENT_ROW || kind==UI_COMPONENT_MENU) {
        s.background=press ? t->pressed : hover ? t->hover : s.selected ? t->tinted : (Clay_Color){0};
        s.radius=kind==UI_COMPONENT_CARD ? 8.0f : 4.0f;
    } else if (kind==UI_COMPONENT_TAB) {
        s.background=s.selected ? t->text : press ? t->tinted_pressed :
            hover ? t->tinted_hover : t->tinted;
        s.foreground=s.selected ? t->on_accent : t->text;
    } else {
        s.foreground=press ? ui_theme_opacity(t->text,t->active_opacity) : hover ? t->text : t->subdued;
        if (s.selected) { s.foreground=t->accent; }
    }
    if (disabled) {
        s.background=ui_theme_opacity(s.background,t->disabled_opacity);
        s.foreground=ui_theme_opacity(s.foreground,t->disabled_opacity);
        s.outline=ui_theme_opacity(s.outline,t->disabled_opacity);
    } else if (state&UI_CONTROL_FOCUS) { s.outline=t->focus; s.border=t->focus_width; }
    return s;
}
static uint32_t interactive(UiWidgets *ui, Clay_ElementId id, UiControl c, bool *activated)
{
    *activated=ui_widgets_button_event(ui,id,(c.state&UI_CONTROL_DISABLED)!=0);
    uint32_t state=c.state;
    if (ui->hover==id.id) { state|=UI_CONTROL_HOVER; }
    if (ui->active==id.id && ui->input.down) { state|=UI_CONTROL_PRESSED; }
    if (ui->focus==id.id) { state|=UI_CONTROL_FOCUS; }
    return state;
}
void ui_component_label(Clay_String label, uint16_t font, uint16_t size, Clay_Color color, bool rtl)
{
    if (label.length<=0 || !label.chars) { return; }
    CLAY_TEXT(label,CLAY_TEXT_CONFIG({.fontId=(uint16_t)(font|(rtl ? FONT_ROLE_RTL : 0)),
        .fontSize=size,.textColor=color,.wrapMode=CLAY_TEXT_WRAP_NONE}));
}
static void content(UiWidgets *ui, Clay_ElementId id, Clay_String label, UiIcon icon,
    UiComponentStyle s, UiControl c)
{
    if (!c.rtl && icon!=UI_ICON_NONE) { ui_icon(ui,child(id,1),icon,20,s.foreground,c.rtl); }
    ui_component_label(label,FONT_ROLE_BOLD,ui_theme()->small,s.foreground,c.rtl);
    if (c.rtl && icon!=UI_ICON_NONE) { ui_icon(ui,child(id,1),icon,20,s.foreground,c.rtl); }
    if (s.selected) { ui_icon(ui,child(id,2),UI_ICON_CHECK,16,s.foreground,c.rtl); }
}
bool ui_component_button(UiWidgets *ui, Clay_ElementId id, Clay_String label,
    UiIcon icon, UiComponentKind kind, UiControl c)
{
    if (!label_valid(label) || kind<UI_COMPONENT_PRIMARY || kind>=UI_COMPONENT_KIND_COUNT || icon<UI_ICON_NONE ||
        icon>=UI_ICON_COUNT || !valid(ui,id,c)) { ui_widgets_fail(ui,UI_WIDGET_INVALID); return false; }
    bool activated=false; uint32_t state=interactive(ui,id,c,&activated);
    UiComponentStyle s=ui_component_style(kind,state);
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(c.width),CLAY_SIZING_FIXED(c.height)},
        .childGap=8,.childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},
        .backgroundColor=s.background,.cornerRadius=CLAY_CORNER_RADIUS(s.radius),
        .border={.color=s.outline,.width=CLAY_BORDER_ALL(s.border)},
        .clip={.horizontal=true,.vertical=true}}) {
        content(ui,id,label,icon,s,c);
    }
    return activated;
}
bool ui_component_row(UiWidgets *ui, Clay_ElementId id, Clay_String label,
    Clay_String detail, UiIcon icon, UiComponentKind kind, UiControl c)
{
    if (!label_valid(label) || !label_valid(detail) ||
        (kind!=UI_COMPONENT_ROW && kind!=UI_COMPONENT_MENU) || icon<UI_ICON_NONE ||
        icon>=UI_ICON_COUNT || !valid(ui,id,c)) { ui_widgets_fail(ui,UI_WIDGET_INVALID); return false; }
    bool activated=false; uint32_t state=interactive(ui,id,c,&activated);
    UiComponentStyle s=ui_component_style(kind,state);
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(c.width),CLAY_SIZING_FIXED(c.height)},
        .padding={12,12,8,8},.childGap=12,.childAlignment={.y=CLAY_ALIGN_Y_CENTER}},
        .backgroundColor=s.background,.cornerRadius=CLAY_CORNER_RADIUS(s.radius),
        .border={.color=s.outline,.width=CLAY_BORDER_ALL(s.border)},
        .clip={.horizontal=true,.vertical=true}}) {
        if (!c.rtl) { ui_icon(ui,child(id,1),icon,24,s.foreground,c.rtl); }
        CLAY_AUTO_ID({.layout={.sizing={.width=CLAY_SIZING_GROW(0)},.childGap=4,
            .layoutDirection=CLAY_TOP_TO_BOTTOM,.childAlignment={.x=c.rtl ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT}},
            .clip={.horizontal=true}}) {
            ui_component_label(label,FONT_ROLE_BODY,ui_theme()->small,s.foreground,c.rtl);
            ui_component_label(detail,FONT_ROLE_BODY,ui_theme()->marginal,
                ui_theme_opacity(ui_theme()->subdued,(state&UI_CONTROL_DISABLED) ? 0.3f : 1),c.rtl);
        }
        if (s.selected) { ui_icon(ui,child(id,2),UI_ICON_CHECK,16,s.foreground,c.rtl); }
        if (c.rtl) { ui_icon(ui,child(id,1),icon,24,s.foreground,c.rtl); }
    }
    return activated;
}
static void artwork_tile(UiWidgets *ui, Clay_ElementId id, UiCardArtwork art, float size, bool disabled)
{
    if (art.texture) {
        UiArtworkCommand *image=ui_widgets_scratch(ui,sizeof(*image),_Alignof(UiArtworkCommand));
        if (!image) { return; }
        *image=(UiArtworkCommand){UI_ARTWORK_COMMAND_MAGIC,art.texture,art.source_width,
            art.source_height,4,disabled ? 0.3f : 1};
        CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(size),CLAY_SIZING_FIXED(size)}},
            .custom={.customData=image},.userData=(void *)&ui_artwork_command_tag}) { }
        return;
    }
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(size),CLAY_SIZING_FIXED(size)},
        .padding={12,12,12,12},.childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},
        .backgroundColor=art.background,.cornerRadius=CLAY_CORNER_RADIUS(4)}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(size*0.62f),CLAY_SIZING_FIXED(size*0.62f)},
            .padding={8,8,8,8},.childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},
            .backgroundColor=art.foreground,.cornerRadius=CLAY_CORNER_RADIUS(art.seed%2 ? size : 8)}) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(size*0.25f),CLAY_SIZING_FIXED(size*0.25f)}},
                .backgroundColor=art.background,.cornerRadius=CLAY_CORNER_RADIUS(size)}) { }
        }
    }
}
bool ui_component_card(UiWidgets *ui, Clay_ElementId id, Clay_String title,
    Clay_String subtitle, UiCardArtwork art, UiControl c)
{
    if (!label_valid(title) || !label_valid(subtitle) ||
        (art.texture && (!art.source_width || !art.source_height ||
            art.source_width>UI_MAX_SURFACE || art.source_height>UI_MAX_SURFACE)) ||
        c.width<64 || c.height<c.width+48 || !valid(ui,id,c)) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return false;
    }
    bool activated=false; uint32_t state=interactive(ui,id,c,&activated);
    UiComponentStyle s=ui_component_style(UI_COMPONENT_CARD,state);
    if (state&UI_CONTROL_DISABLED) {
        art.background=ui_theme_opacity(art.background,0.3f);
        art.foreground=ui_theme_opacity(art.foreground,0.3f);
    }
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(c.width),CLAY_SIZING_FIXED(c.height)},
        .padding={12,12,12,12},.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
        .backgroundColor=s.background,.cornerRadius=CLAY_CORNER_RADIUS(s.radius),
        .border={.color=s.outline,.width=CLAY_BORDER_ALL(s.border)},
        .clip={.horizontal=true,.vertical=true}}) {
        artwork_tile(ui,child(id,1),art,c.width-24,(state&UI_CONTROL_DISABLED)!=0);
        CLAY_AUTO_ID({.layout={.sizing={.width=CLAY_SIZING_GROW(0)},.childGap=4}}) {
            ui_component_label(title,FONT_ROLE_BOLD,14,s.foreground,c.rtl);
            if (s.selected) { ui_icon(ui,child(id,2),UI_ICON_CHECK,16,s.foreground,c.rtl); }
        }
        ui_component_label(subtitle,FONT_ROLE_BODY,12,
            ui_theme_opacity(ui_theme()->subdued,(state&UI_CONTROL_DISABLED) ? 0.3f : 1),c.rtl);
    }
    return activated;
}
void ui_component_status(UiWidgets *ui, Clay_ElementId id, Clay_String label,
    UiStatusKind kind, float width, bool rtl)
{
    if (!label_valid(label) || kind<UI_STATUS_INFORMATION || kind>UI_STATUS_EMPTY ||
        !valid(ui,id,(UiControl){.width=width,.height=48})) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return;
    }
    const UiTheme *t=ui_theme(); Clay_Color color=kind==UI_STATUS_WARNING ? t->warning :
        kind==UI_STATUS_ERROR ? t->negative : kind==UI_STATUS_INFORMATION ? t->information : t->subdued;
    UiIcon icon=kind==UI_STATUS_WARNING || kind==UI_STATUS_ERROR ? UI_ICON_WARNING : UI_ICON_INFO;
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(48)},
        .padding={12,12,8,8},.childGap=12,.childAlignment={.y=CLAY_ALIGN_Y_CENTER}},
        .backgroundColor=t->elevated,.cornerRadius=CLAY_CORNER_RADIUS(8),
        .clip={.horizontal=true,.vertical=true}}) {
        if (!rtl) { ui_icon(ui,child(id,1),icon,24,color,rtl); }
        ui_component_label(label,FONT_ROLE_BODY,14,color,rtl);
        if (rtl) { ui_icon(ui,child(id,1),icon,24,color,rtl); }
    }
}
bool ui_focus_move(UiWidgets *ui, const UiFocusItem *items, uint32_t count, UiFocusKey key, bool rtl)
{
    if (!ui || !items || !count || count>400 || key<UI_FOCUS_NEXT || key>UI_FOCUS_END) { return false; }
    int32_t current=-1; bool backward=key==UI_FOCUS_PREVIOUS || key==UI_FOCUS_UP ||
        (key==UI_FOCUS_LEFT && !rtl) || (key==UI_FOCUS_RIGHT && rtl) || key==UI_FOCUS_END;
    for (uint32_t i=0;i<count;++i) {
        if (!items[i].id.id) { return false; }
        if (items[i].id.id==ui->focus) { current=(int32_t)i; }
    }
    if (key==UI_FOCUS_HOME || key==UI_FOCUS_END) { current=-1; }
    for (uint32_t n=0;n<count;++n) {
        current=current<0 ? (backward ? (int32_t)count-1 : 0) :
            (current+(backward ? (int32_t)count-1 : 1))%(int32_t)count;
        if (!items[current].disabled) {
            bool changed=ui->focus!=items[current].id.id; ui->focus=items[current].id.id; return changed;
        }
    }
    return false;
}
