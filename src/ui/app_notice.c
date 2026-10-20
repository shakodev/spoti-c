#include "app.h"
#include <math.h>
#include <string.h>

static const char overflow[] = "The service message exceeds the 1023-byte limit. "
    "Its diagnostic was not retained. Retry the action. If it fails again, sign out and sign in again.";
static const char invalid[] = "The service returned an invalid UTF-8 message. "
    "Retry the action. If it fails again, sign out and sign in again.";

AppResult app_set_notice(App *a, AppSeverity severity, Clay_String text)
{
    if (!a || !a->alive || a->widgets.in_frame || severity>APP_FAILURE || severity<APP_INFORMATION ||
        text.length<0 || (text.length && !text.chars)) { return APP_INVALID; }
    uint32_t units; AppResult result=APP_OK;
    if (text.length>=(int32_t)APP_NOTICE_BYTES) {
        text=(Clay_String){true,sizeof(overflow)-1,overflow}; severity=APP_FAILURE; result=APP_FULL;
    } else if ((text.length && memchr(text.chars,0,(size_t)text.length)) ||
        font_utf8_to_utf16((Clay_StringSlice){text.length,text.chars,text.chars},NULL,0,&units)!=FONT_OK) {
        text=(Clay_String){true,sizeof(invalid)-1,invalid}; severity=APP_FAILURE; result=APP_INVALID;
    }
    if (text.length) { memmove(a->model.notice,text.chars,(size_t)text.length); }
    a->model.notice[text.length]=0; a->model.notice_severity=severity;
    if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_STATE); }
    if (a->services.wake) { a->services.wake(a->services.context); }
    return result;
}
static Clay_String literal(const char *text) { return (Clay_String){true,(int32_t)strlen(text),text}; }
static bool preview(Clay_String text)
{
    /* Keep even alternating-script/bidi runs within the 64-patch renderer
     * budget. Longer supported diagnostics remain intact in Read messages. */
    if (text.length>64) { return false; }
    unsigned token=0, lines=0;
    for (int32_t i=0;i<text.length;++i) {
        unsigned char ch=(unsigned char)text.chars[i];
        if (ch=='\n' && ++lines>1) { return false; }
        if (ch<=' ') { token=0; } else if (++token>32) { return false; }
    }
    return true;
}
void app_message(App *a, Clay_String text, AppSeverity severity)
{
    if (!text.length) { return; }
    const char *label=severity==APP_FAILURE ? "Error" : severity==APP_WARNING ? "Warning" : "Information";
    AppMessages *m=&a->messages;
    static const char full[]="\r\nAdditional messages exceed the supported limit. Retry this page to refresh its diagnostics.";
    size_t heading=strlen(label), bytes=(size_t)text.length;
    if (!m->full && bytes+heading+6 <= APP_MESSAGE_BYTES-m->length-sizeof(full)) {
        char *out=m->text+m->length;
        memcpy(out,label,heading); out+=heading; memcpy(out,": ",2); out+=2;
        /* Native multiline EDIT requires CRLF for explicit line breaks. */
        for (size_t i=0;i<bytes;++i) {
            if (text.chars[i]=='\n' && (!i || text.chars[i-1]!='\r')) {
                if ((size_t)(out-m->text)+bytes-i+sizeof(full)>=APP_MESSAGE_BYTES) { m->full=true; break; }
                *out++='\r';
            }
            *out++=text.chars[i];
        }
        if (!m->full) { memcpy(out,"\r\n\r\n",4); out+=4; m->length=(uint32_t)(out-m->text); *out=0; }
    } else { m->full=true; }
    if (m->full && !strstr(m->text,full)) {
        memcpy(m->text+m->length,full,sizeof(full)); m->length+=(uint32_t)sizeof(full)-1;
    }
    Clay_Color color=severity==APP_FAILURE ? (Clay_Color){255,185,185,255} :
        severity==APP_WARNING ? (Clay_Color){255,205,150,255} : (Clay_Color){185,215,255,255};
    Clay_Color background=severity==APP_FAILURE ? (Clay_Color){65,25,25,255} :
        severity==APP_WARNING ? (Clay_Color){60,42,22,255} : (Clay_Color){25,38,55,255};
    float width=fmaxf(1,a->geometry.main-24);
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIT()},.padding=CLAY_PADDING_ALL(6),
        .childGap=2,.layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor=background,.cornerRadius=CLAY_CORNER_RADIUS(4)}) {
        CLAY_TEXT(literal(label),CLAY_TEXT_CONFIG({.fontId=FONT_FACE_NOTICE,.fontSize=12,.lineHeight=16,.textColor=color}));
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(fmaxf(1,width-12)),CLAY_SIZING_FIT(0,60)}},.clip={.vertical=true}}) {
            Clay_String visible=preview(text) ? text : CLAY_STRING("Open Read messages for the full message and recovery details.");
            CLAY_TEXT(visible,CLAY_TEXT_CONFIG({.fontId=FONT_FACE_NOTICE,.fontSize=14,.lineHeight=20,
                .wrapMode=CLAY_TEXT_WRAP_WORDS,.textColor=color}));
        }
    }
}
AppResult app_messages_request(App *a)
{
    if (!a || !a->alive || a->widgets.in_frame) { return APP_INVALID; }
    const char *text=a->messages.length ? a->messages.text : "Information: No current status messages.";
    strcpy_s(a->messages.details,sizeof(a->messages.details),text); a->messages.requested=true;
    return APP_OK;
}
