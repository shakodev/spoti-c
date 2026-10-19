#include "playlist.h"
#include "ui/app.h"
#include <imm.h>
#include <string.h>

bool playlist_text_message(App *a, void *hwnd, uint32_t message, uintptr_t wp, intptr_t lp)
{
    Playlist *p=a->services.playlist;
    if (!p || !p->active || a->model.location.screen!=APP_PLAYLIST || a->widgets.focus!=CLAY_ID("PlaylistFilter").id) { return false; }
    UiTextInput next=a->widgets.texts[0]; UiWidgetResult result=UI_WIDGET_OK; bool handled=true;
    if (message==WM_CHAR) {
        uint16_t value=(uint16_t)wp, pair[2]; uint32_t n=1;
        if (value>=0xd800 && value<=0xdbff) { p->surrogate=value; return true; }
        pair[0]=value;
        if (p->surrogate) { pair[0]=p->surrogate; pair[1]=value; n=2; p->surrogate=0; }
        if (value>=32) { result=ui_text_ime_utf16(&next,pair,n,n,true); }
    } else if (message==WM_IME_COMPOSITION) { result=ui_text_ime_message(&next,hwnd,lp); }
    else if (message==WM_IME_ENDCOMPOSITION || message==WM_KILLFOCUS) { ui_text_composition_cancel(&next); p->surrogate=0; }
    else if (message==WM_KEYDOWN) {
        bool shift=GetKeyState(VK_SHIFT)<0, control=GetKeyState(VK_CONTROL)<0;
        UiTextKey key;
        switch (wp) {
        case VK_LEFT: key=UI_TEXT_LEFT; break; case VK_RIGHT: key=UI_TEXT_RIGHT; break;
        case VK_HOME: key=UI_TEXT_HOME; break; case VK_END: key=UI_TEXT_END; break;
        case VK_BACK: key=UI_TEXT_BACKSPACE; break; case VK_DELETE: key=UI_TEXT_DELETE; break;
        default: key=(UiTextKey)-1; break;
        }
        if ((int)key>=0) { result=ui_text_key(&next,key,shift); }
        else if (control && wp=='A') { result=ui_text_key(&next,UI_TEXT_SELECT_ALL,false); }
        else if (control && (wp=='C' || wp=='X' || wp=='V')) {
            UiClipboard clipboard=ui_clipboard_win32(hwnd);
            result=ui_text_clipboard(&next,&clipboard,wp=='V',wp=='X');
        } else { handled=false; }
    } else { handled=false; }
    if (!handled) { return false; }
    if (next.length>=TRACK_QUERY_BYTES || next.composition_length>=TRACK_QUERY_BYTES) { result=UI_WIDGET_FULL; }
    if (result==UI_WIDGET_OK) {
        bool changed=strcmp(next.text,a->widgets.texts[0].text)!=0; a->widgets.texts[0]=next;
        if (changed) { (void)app_enqueue(a,(AppAction){.type=APP_TRACK_QUERY}); }
    } else { a->model.last_result=result==UI_WIDGET_FULL ? APP_FULL : APP_INVALID; }
    if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_INPUT); } return message!=WM_KILLFOCUS;
}
