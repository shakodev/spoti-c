#include "app.h"
#include <string.h>

void app_player_icon(App *a,Clay_ElementId id,UiIcon icon,AppAction action,const char *label,
    const char *unavailable,bool selected,bool primary)
{
    if (a->control_count==APP_CONTROLS || a->now.glyph_count==24) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return; }
    /* Unavailable controls remain focusable to expose their reason on keyboard. */
    if (unavailable) {
        action=(AppAction){.type=APP_PLAYER_INFO};
        strcpy_s(action.entity.title,sizeof(action.entity.title),unavailable);
    }
    a->controls[a->control_count++]=(AppControl){.id=id,.action=action};
    bool hit=ui_button_behavior(&a->widgets,id,false);
    bool over=a->widgets.hover==id.id, focus=a->widgets.focus==id.id, pressed=a->widgets.active==id.id;
    AppGlyph *glyph=&a->now.glyphs[a->now.glyph_count++];
    *glyph=(AppGlyph){.tag=APP_CUSTOM_ICON,.icon=icon};
    float color=unavailable ? 0.30f : primary ? 0.03f : pressed ? 0.5f : over || focus ? 1.0f : 0.7f;
    glyph->color[0]=glyph->color[1]=glyph->color[2]=color; glyph->color[3]=1;
    if (selected && !unavailable) { glyph->color[0]=0.12f; glyph->color[1]=0.84f; glyph->color[2]=0.38f; }
    Clay_Color background=primary ? (unavailable ? (Clay_Color){90,90,90,255} :
        pressed ? (Clay_Color){170,170,170,255} : (Clay_Color){255,255,255,255}) : (Clay_Color){0};
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(32),CLAY_SIZING_FIXED(32)},
        .childAlignment={.x=CLAY_ALIGN_X_CENTER,.y=CLAY_ALIGN_Y_CENTER}},.backgroundColor=background,
        .cornerRadius=CLAY_CORNER_RADIUS(16),.border={.color={235,235,235,255},.width=CLAY_BORDER_ALL(focus ? 1 : 0)}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(primary ? 18.0f : 16.0f),CLAY_SIZING_FIXED(primary ? 18.0f : 16.0f)}},
            .custom={.customData=glyph}}) { }
        if (selected && !unavailable) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(3),CLAY_SIZING_FIXED(3)}},
                .floating={.attachTo=CLAY_ATTACH_TO_PARENT,.attachPoints={.element=CLAY_ATTACH_POINT_CENTER_BOTTOM,
                    .parent=CLAY_ATTACH_POINT_CENTER_BOTTOM},.offset={0,-1},.pointerCaptureMode=CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .backgroundColor={30,215,96,255},.cornerRadius=CLAY_CORNER_RADIUS(2)}) { }
        }
    }
    if (over || focus) { a->now.tooltip=unavailable ? unavailable : label; a->now.tooltip_id=id; }
    if (hit) { (void)app_enqueue(a,action); }
}
void app_player_tooltip(App *a)
{
    if (!a->now.tooltip) { return; }
    /* Keep full labels in the keyboard-accessible Read messages transcript. */
    Clay_String text={false,(int32_t)strlen(a->now.tooltip),a->now.tooltip};
    AppMessages *messages=&a->messages;
    if ((uint32_t)text.length+12<APP_MESSAGE_BYTES-messages->length) {
        char *out=messages->text+messages->length;
        memcpy(out,"Control: ",9); memcpy(out+9,text.chars,(size_t)text.length);
        memcpy(out+9+text.length,"\r\n",3); messages->length+=11u+(uint32_t)text.length;
    }
    CLAY(CLAY_ID("PlayerTooltip"),{.layout={.sizing={CLAY_SIZING_FIT(0,340),CLAY_SIZING_FIT()},
        .padding=CLAY_PADDING_ALL(8)},.floating={.attachTo=CLAY_ATTACH_TO_ELEMENT_WITH_ID,
        .parentId=CLAY_ID("AppNowPlaying").id,.attachPoints={.element=CLAY_ATTACH_POINT_CENTER_BOTTOM,
            .parent=CLAY_ATTACH_POINT_CENTER_TOP},.offset={0,-8},.zIndex=20,
        .pointerCaptureMode=CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
        .backgroundColor={45,45,45,255},.cornerRadius=CLAY_CORNER_RADIUS(4)}) {
        CLAY_TEXT(text,CLAY_TEXT_CONFIG({.fontId=FONT_FACE_NOTICE,.fontSize=14,.lineHeight=20,
            .wrapMode=CLAY_TEXT_WRAP_WORDS,.textColor={240,240,240,255}}));
    }
}
