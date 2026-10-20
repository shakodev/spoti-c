#include "window.h"
#include <dwmapi.h>

static const wchar_t window_class[] = L"SpoticWindow25";

static uint64_t now_ms(const UiWindow *w) { return core_clock_now(&w->config.clock); }
static int scaled(int value, UINT dpi) { return MulDiv(value, (int)dpi, 96); }

static void motion_preference(UiWindow *w)
{
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0)) {
        ui_scheduler_reduce_motion(&w->scheduler,enabled == FALSE);
    }
}

static void render_dirty(UiWindow *w)
{
    uint32_t reasons = ui_scheduler_frame(&w->scheduler, now_ms(w));
    if (reasons) {
        if (w->config.prepare) { w->config.prepare(w->config.context, w, reasons); }
        if (w->closed) { return; }
        metrics_count(w->config.metrics, METRIC_RENDER, 1);
        w->config.frame(w->config.context, w, reasons);
    }
}

static void visibility(UiWindow *w)
{
    ui_scheduler_visibility(&w->scheduler, IsWindowVisible(w->hwnd) && !IsIconic(w->hwnd),
        false, now_ms(w));
}

void ui_window_set_occluded(UiWindow *w, bool occluded)
{
    if (GetCurrentThreadId() != w->owner) { return; }
    ui_scheduler_visibility(&w->scheduler, w->scheduler.visible, occluded, now_ms(w));
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    UiWindow *w = (UiWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (message == WM_NCCREATE) {
        w = ((CREATESTRUCTW *)lp)->lpCreateParams;
        w->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
    }
    if (!w) { return DefWindowProcW(hwnd, message, wp, lp); }
    /* DWM owns caption hit testing and nonclient pointer tracking on composed
     * desktops. Keep it ahead of the application's client-input router. */
    LRESULT dwm_result;
    if (DwmDefWindowProc(hwnd, message, wp, lp, &dwm_result)) {
        return dwm_result;
    }
    LRESULT routed = 0;
    if (w->config.input && w->config.input(w->config.context,w,message,wp,lp,&routed)) { return routed; }
    switch (message) {
    case UI_WINDOW_OCCLUSION_MESSAGE:
        ui_window_set_occluded(w, false);
        ui_scheduler_dirty(&w->scheduler, UI_DIRTY_PAINT);
        return 0;
    case WM_CREATE: {
        w->dpi = GetDpiForWindow(hwnd);
        motion_preference(w);
        /* Keep the caption outside the opaque DX11 client surface. Native
         * layout/drawing/hit testing own the same button rectangles and states.
         * Older Windows versions simply retain their default native theme. */
        BOOL dark = TRUE;
        (void)DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        break;
    }
    case WM_SETTINGCHANGE:
        motion_preference(w);
        break;
    case WM_DPICHANGED: {
        w->dpi = HIWORD(wp);
        RECT *rect = (RECT *)lp;
        SetWindowPos(hwnd, NULL, rect->left, rect->top, rect->right - rect->left,
            rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        ui_scheduler_dirty(&w->scheduler, UI_DIRTY_SIZE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lp;
        UINT dpi = w->dpi ? w->dpi : 96;
        info->ptMinTrackSize = (POINT){scaled(400, dpi), scaled(240, dpi)};
        return 0;
    }
    case WM_SIZE:
        visibility(w);
        ui_scheduler_dirty(&w->scheduler, UI_DIRTY_SIZE);
        break;
    case WM_SHOWWINDOW:
        /* WM_SHOWWINDOW arrives before IsWindowVisible reflects the new state. */
        ui_scheduler_visibility(&w->scheduler, wp != 0 && !IsIconic(hwnd),
            false, now_ms(w));
        break;
    case WM_WINDOWPOSCHANGED:
        visibility(w);
        break;
    case WM_ENTERSIZEMOVE: w->sizing = true; break;
    case WM_EXITSIZEMOVE: w->sizing = false; break;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        BeginPaint(hwnd, &paint); EndPaint(hwnd, &paint);
        /* An external paint is a presentation opportunity after occlusion. */
        visibility(w);
        ui_scheduler_dirty(&w->scheduler, UI_DIRTY_PAINT);
        /* Win32 runs a modal message loop during caption drag/resize. Paints
         * still render on demand there; no timer or private polling loop. */
        if (w->sizing) { render_dirty(w); }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DEVICECHANGE:
    case WM_DISPLAYCHANGE:
        ui_window_set_occluded(w, false);
        ui_scheduler_dirty(&w->scheduler, UI_DIRTY_DEVICE);
        if (w->config.device) { w->config.device(w->config.context, w); }
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) {
            ui_window_set_occluded(w, false);
            ui_scheduler_dirty(&w->scheduler, UI_DIRTY_PAINT);
        }
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS:
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_IME_STARTCOMPOSITION: case WM_IME_COMPOSITION: case WM_IME_ENDCOMPOSITION:
    case WM_MOUSEMOVE: case WM_MOUSELEAVE: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        ui_scheduler_dirty(&w->scheduler, UI_DIRTY_INPUT);
        break;
    case WM_CLOSE:
        w->closed = true;
        return 0;
    case WM_NCDESTROY:
        w->closed = true; w->hwnd = NULL;
        if (w->hwnd_metric.owner) { (void)metrics_handle_release(&w->hwnd_metric); }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    default: break;
    }
    /* Native system menu, caption buttons, snap, Alt+F4, IME, keyboard focus
     * and WM_GETOBJECT retain their standard accessible OS behavior. */
    return DefWindowProcW(hwnd, message, wp, lp);
}

bool ui_window_create(UiWindow *w, const UiWindowConfig *config)
{
    if (!w || !config || w->hwnd || config->width < 400 || config->height < 240 ||
        !config->title || !config->frame || !config->workers ||
        !event_wait_handle(config->workers)) { return false; }
    *w = (UiWindow){0}; w->config = *config; w->owner = GetCurrentThreadId();
    ui_scheduler_init(&w->scheduler);
    WNDCLASSEXW cls = {0};
    cls.cbSize = sizeof(cls); cls.lpfnWndProc = window_proc;
    cls.hInstance = config->instance; cls.lpszClassName = window_class;
    cls.hCursor = LoadCursorW(NULL, IDC_ARROW);
    cls.hIcon = LoadIconW(NULL, IDI_APPLICATION); cls.hIconSm = cls.hIcon;
    /* One owning window per class: explicit failure rather than shared lifetime. */
    if (!RegisterClassExW(&cls)) { return false; }
    w->class_owned = true;
    HWND hwnd = CreateWindowExW(0, window_class, config->title, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, config->width, config->height,
        NULL, NULL, config->instance, w);
    if (!hwnd) { (void)ui_window_destroy(w); return false; }
    if (config->metrics && !metrics_handle_acquire(config->metrics, &w->hwnd_metric, (uintptr_t)hwnd)) {
        (void)ui_window_destroy(w); return false;
    }
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    if (config->show) { ShowWindow(hwnd, SW_SHOWNORMAL); }
    visibility(w);
    return true;
}

bool ui_window_destroy(UiWindow *w)
{
    if (!w || GetCurrentThreadId() != w->owner) { return false; }
    if (w->hwnd && !DestroyWindow(w->hwnd)) { return false; }
    if (w->class_owned && !UnregisterClassW(window_class, w->config.instance)) { return false; }
    w->class_owned = false;
    return true;
}

bool ui_window_step(UiWindow *w, uint32_t max_wait_ms)
{
    if (!w || GetCurrentThreadId() != w->owner || w->closed || w->failed) { return false; }
    MSG message;
    /* A bounded drain keeps continuous input from starving worker/frame work.
     * Remaining messages stay available to MWMO_INPUTAVAILABLE next turn. */
    unsigned messages = 0;
    while (messages++ < 256 && PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        ++w->stats.messages;
        if (message.message == WM_QUIT) { w->closed = true; break; }
        TranslateMessage(&message); DispatchMessageW(&message);
    }
    Event events[32]; size_t count;
    CoreResult result = event_drain(w->config.workers, events, 32, &count);
    if (result != CORE_OK && result != CORE_CLOSED) { w->failed = true; return false; }
    for (size_t i = 0; i < count; ++i) {
        ++w->stats.worker_events;
        ui_scheduler_dirty(&w->scheduler, UI_DIRTY_STATE);
        if (w->config.worker) { w->config.worker(w->config.context, w, &events[i]); }
    }
    if (result == CORE_CLOSED) { w->closed = true; }
    if (w->closed) { return false; }
    render_dirty(w);
    if (w->closed) { return false; }
    uint32_t timeout = ui_scheduler_wait(&w->scheduler, now_ms(w));
    if (timeout > max_wait_ms) { timeout = max_wait_ms; }
    w->stats.last_wait_ms = timeout; ++w->stats.waits;
    HANDLE wake = event_wait_handle(w->config.workers);
    DWORD wait = MsgWaitForMultipleObjectsEx(1, &wake, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (wait == WAIT_FAILED) { w->failed = true; return false; }
    if (wait == WAIT_TIMEOUT) { ++w->stats.timeouts; }
    else { ++w->stats.wakes; metrics_count(w->config.metrics, METRIC_WAKE, 1); }
    return true;
}
