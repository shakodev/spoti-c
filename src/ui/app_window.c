#include "app_desktop.h"
#include "screens/playlist.h"
#include <windowsx.h>

void app_desktop_frame(void *context, UiWindow *window, uint32_t dirty)
{
    AppDesktop *d = context; ui_surface_frame(&d->surface,window,dirty);
    if (d->startup && !d->startup->times.ticks[STARTUP_PRESENT] && d->surface.ready &&
        d->surface.last_result==UI_RENDER_OK && !font_stats(&d->surface.fonts).pending && d->app.frames) {
        startup_mark(&d->startup->times,STARTUP_PRESENT); d->startup->times.useful=1;
        ui_scheduler_dirty(&window->scheduler,UI_DIRTY_STATE);
    }
}
void app_desktop_prepare(void *context, UiWindow *window, uint32_t dirty)
{
    AppDesktop *d = context;
    ui_surface_prepare(&d->surface,window,dirty);
    if (!d->surface.ready || !d->app.alive) { return; }
    if (d->startup_prepare) { d->startup_prepare(window); }
    app_refresh_clock(&d->app,dirty);
    if (d->fixture) { app_fixture_pump(d->fixture); }
    (void)app_update(&d->app,GetTickCount64());
    RECT rect; GetClientRect(window->hwnd,&rect);
    uint32_t count = d->app.input_count; d->app.input_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        (void)app_layout(&d->app,(float)rect.right,(float)rect.bottom,(float)window->dpi,d->app.inputs[i]);
        (void)app_update(&d->app,GetTickCount64());
    }
    app_desktop_media_prepare(d,GetTickCount64());
    if (d->app.messages.requested) {
        d->app.messages.requested=false;
        if (!app_messages_dialog(window->hwnd,d->app.messages.details)) {
            (void)app_set_notice(&d->app,APP_FAILURE,CLAY_STRING("Could not open message details. Try Read messages again."));
        }
        ui_scheduler_dirty(&window->scheduler,UI_DIRTY_STATE);
    }
    (void)font_prepare_pending(&d->surface.fonts);
    (void)font_upload_renderer(&d->surface.fonts,&d->surface.renderer);
}
bool app_desktop_input(void *context, UiWindow *w, UINT message, WPARAM wp, LPARAM lp, LRESULT *out)
{
    AppDesktop *d = context; *out = 0;
    if (message == WM_GETMINMAXINFO) {
        UINT dpi = w->dpi ? w->dpi : 96;
        ((MINMAXINFO *)lp)->ptMinTrackSize = (POINT){MulDiv(420,(int)dpi,96),MulDiv(560,(int)dpi,96)}; return true;
    }
    if (!d->app.alive) { return false; }
    if (d->media.alive && message==WM_APPCOMMAND &&
        media_controls_appcommand(&d->media,GET_APPCOMMAND_LPARAM(lp),GetTickCount64())!=MEDIA_UNSUPPORTED) { *out=TRUE; return true; }
    if (d->media.alive && message==WM_KEYDOWN &&
        media_controls_key(&d->media,(unsigned)wp,(lp&((LPARAM)1<<30))!=0,GetTickCount64())!=MEDIA_UNSUPPORTED) { return true; }
    if (playlist_text_message(&d->app,w->hwnd,message,wp,lp)) { return true; }
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        AppKey key;
        switch (wp) {
        case VK_TAB: key=APP_KEY_TAB; break;
        case VK_RETURN: case VK_SPACE: key=APP_KEY_ENTER; break;
        case VK_BROWSER_BACK: key=APP_KEY_BACK; break;
        case VK_LEFT: key=GetKeyState(VK_MENU)<0 ? APP_KEY_BACK : APP_KEY_LEFT; break;
        case VK_RIGHT: key=APP_KEY_RIGHT; break;
        case VK_UP: key=APP_KEY_UP; break;
        case VK_DOWN: key=APP_KEY_DOWN; break;
        case VK_HOME: key=APP_KEY_HOME; break;
        case VK_END: key=APP_KEY_END; break;
        case VK_PRIOR: key=APP_KEY_PAGE_UP; break;
        case VK_NEXT: key=APP_KEY_PAGE_DOWN; break;
        default: return false;
        }
        (void)app_key(&d->app,key,GetKeyState(VK_SHIFT)<0);
    } else if (message == WM_MOUSEWHEEL) {
        app_scroll(&d->app,-(double)GET_WHEEL_DELTA_WPARAM(wp)*96/WHEEL_DELTA);
    } else if (message == WM_MOUSELEAVE) {
        if (!d->pointer.down) { d->pointer.x=d->pointer.y=-1; }
        d->pointer.pressed=d->pointer.released=d->pointer.cancel=false;
        (void)app_pointer(&d->app,d->pointer);
    } else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_MOUSEMOVE ||
        message == WM_CAPTURECHANGED || message == WM_KILLFOCUS) {
        bool cancel = message == WM_CAPTURECHANGED || message == WM_KILLFOCUS;
        if (message == WM_MOUSEMOVE) { TRACKMOUSEEVENT track={sizeof(track),TME_LEAVE,w->hwnd,0}; (void)TrackMouseEvent(&track); }
        if (!cancel) { d->pointer.x=(float)GET_X_LPARAM(lp)*96/w->dpi; d->pointer.y=(float)GET_Y_LPARAM(lp)*96/w->dpi; }
        d->pointer.pressed = message == WM_LBUTTONDOWN; d->pointer.released = message == WM_LBUTTONUP;
        d->pointer.cancel = cancel;
        if (d->pointer.pressed) { d->pointer.down=true; SetFocus(w->hwnd); SetCapture(w->hwnd); }
        if (d->pointer.released || cancel) { d->pointer.down=false; }
        (void)app_pointer(&d->app,d->pointer);
        if (d->pointer.released && GetCapture()==w->hwnd) { ReleaseCapture(); }
    } else { return false; }
    ui_scheduler_dirty(&w->scheduler,UI_DIRTY_INPUT); return true;
}
