#define COBJMACROS
#include "ui/window.h"
#include "support/test_check.h"
#include <oleacc.h>
#include <string.h>
#include <process.h>
#include <stdatomic.h>

typedef struct Harness {
    UiWindow window;
    EventQueue *queue;
    _Atomic uint32_t delivered;
    uint64_t frames, devices;
} Harness;

static void frame(void *context, UiWindow *window, uint32_t reasons)
{
    Harness *h = context;
    CHECK(window == &h->window && reasons != 0);
    ++h->frames;
}

static void worker(void *context, UiWindow *window, const Event *event)
{
    Harness *h = context;
    CHECK(window == &h->window && event->type == UI_DIRTY_STATE);
    atomic_fetch_add(&h->delivered, 1);
}

static void device(void *context, UiWindow *window)
{
    Harness *h = context; CHECK(window == &h->window); ++h->devices;
}

static unsigned __stdcall producer(void *context)
{
    Harness *h = context;
    for (unsigned i = 0; i < 2000; ++i) {
        Event event = {0}; event.type = UI_DIRTY_STATE; event.id = i;
        CoreResult result;
        do { result = event_post(h->queue, &event); if (result == CORE_FULL) { SwitchToThread(); } }
        while (result == CORE_FULL);
        CHECK(result == CORE_OK);
    }
    return 0;
}

static void hit_tests(void)
{
    for (UINT dpi = 96; dpi <= 192; dpi += 48) {
        CHECK(ui_window_hit_test(0, 0, 1200, 800, dpi, false) == HTTOPLEFT);
        CHECK(ui_window_hit_test(1199, 0, 1200, 800, dpi, false) == HTTOPRIGHT);
        CHECK(ui_window_hit_test(0, 799, 1200, 800, dpi, false) == HTBOTTOMLEFT);
        CHECK(ui_window_hit_test(1199, 799, 1200, 800, dpi, false) == HTBOTTOMRIGHT);
        CHECK(ui_window_hit_test(500, 0, 1200, 800, dpi, false) == HTTOP);
        CHECK(ui_window_hit_test(0, 400, 1200, 800, dpi, false) == HTLEFT);
        CHECK(ui_window_hit_test(500, 400, 1200, 800, dpi, false) == HTCLIENT);
        int y = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) + 2;
        CHECK(ui_window_hit_test(500, y, 1200, 800, dpi, false) == HTCAPTION);
        CHECK(ui_window_hit_test(1170, y, 1200, 800, dpi, false) == HTCLOSE);
        CHECK(ui_window_hit_test(1200 - MulDiv(70, (int)dpi, 96), y, 1200, 800, dpi, false) == HTMAXBUTTON);
        CHECK(ui_window_hit_test(500, 0, 1200, 800, dpi, true) == HTCAPTION);
        CHECK(ui_window_hit_test(-1, 0, 1200, 800, dpi, false) == HTNOWHERE);
    }
}

static void geometry(Harness *h)
{
    UiWindow *w = &h->window;
    CHECK(SetWindowPos(w->hwnd, NULL, -20000, -20000, 900, 650, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT rect; CHECK(GetWindowRect(w->hwnd, &rect));
    CHECK(rect.right - rect.left == 900 && rect.bottom - rect.top == 650);
    RECT suggested = {-20000, -20000, -19000, -19300};
    SendMessageW(w->hwnd, WM_DPICHANGED, MAKEWPARAM(144, 144), (LPARAM)&suggested);
    CHECK(w->dpi == 144);
    CHECK(GetWindowRect(w->hwnd, &rect) && EqualRect(&rect, &suggested));
    wchar_t title[32]; CHECK(GetWindowTextW(w->hwnd, title, 32) > 0);
    CHECK(wcscmp(title, L"spoti-c window test") == 0);
    IAccessible *accessible = NULL;
    CHECK(SUCCEEDED(AccessibleObjectFromWindow(w->hwnd, OBJID_WINDOW,
        &IID_IAccessible, (void **)&accessible)));
    VARIANT self; VariantInit(&self); V_VT(&self) = VT_I4; V_I4(&self) = CHILDID_SELF;
    BSTR name = NULL;
    CHECK(SUCCEEDED(IAccessible_get_accName(accessible, self, &name)));
    CHECK(name && wcscmp(name, L"spoti-c window test") == 0);
    SysFreeString(name); IAccessible_Release(accessible);
    accessible = NULL;
    CHECK(SUCCEEDED(AccessibleObjectFromWindow(w->hwnd, (DWORD)OBJID_TITLEBAR,
        &IID_IAccessible, (void **)&accessible)));
    long children = 0;
    CHECK(SUCCEEDED(IAccessible_get_accChildCount(accessible, &children)) && children >= 5);
    IAccessible_Release(accessible);
    SendMessageW(w->hwnd, WM_DEVICECHANGE, 0, 0); CHECK(h->devices == 1);
    SendMessageW(w->hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
    SendMessageW(w->hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
    ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
    CHECK(ui_window_step(w, 0)); CHECK(w->scheduler.visible);
    SetFocus(w->hwnd); CHECK(GetFocus() == w->hwnd);
    SendMessageW(w->hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    CHECK(IsZoomed(w->hwnd));
    SendMessageW(w->hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
    CHECK(!IsZoomed(w->hwnd));
    CHECK(ui_window_step(w, 0));
    uint64_t frames = h->frames;
    SendMessageW(w->hwnd, WM_ENTERSIZEMOVE, 0, 0);
    CHECK(InvalidateRect(w->hwnd, NULL, FALSE));
    CHECK(UpdateWindow(w->hwnd));
    CHECK(h->frames == frames + 1);
    SendMessageW(w->hwnd, WM_EXITSIZEMOVE, 0, 0);
    ui_window_set_occluded(w, true);
    CHECK(w->scheduler.occluded);
    CHECK(InvalidateRect(w->hwnd, NULL, FALSE)); CHECK(UpdateWindow(w->hwnd));
    CHECK(!w->scheduler.occluded);
    ShowWindow(w->hwnd, SW_MINIMIZE); CHECK(ui_window_step(w, 0));
    CHECK(!w->scheduler.visible && ui_scheduler_wait(&w->scheduler, GetTickCount64()) == INFINITE);
    ShowWindow(w->hwnd, SW_SHOWNOACTIVATE); CHECK(ui_window_step(w, 0));
    ShowWindow(w->hwnd, SW_HIDE); CHECK(ui_window_step(w, 0));
    CHECK(!w->scheduler.visible);
}

static void idle_trace(Harness *h, uint32_t duration)
{
    UiWindow *w = &h->window;
    /* A real visible offscreen HWND, no GPU dependency. DX11 Present tracing
     * belongs to the renderer issue. The frame callback counts submissions. */
    ShowWindow(w->hwnd, SW_SHOWNOACTIVATE);
    for (unsigned i = 0; i < 10; ++i) { CHECK(ui_window_step(w, 0)); }
    /* The desktop can send delayed show/minimize/DWM messages. Drain those
     * finite wakes before selecting an uninterrupted idle observation window. */
    bool quiet = false;
    for (unsigned attempt = 0; attempt < 20 && !quiet; ++attempt) {
        uint64_t frames = h->frames, start = GetTickCount64();
        uint64_t timeouts = w->stats.timeouts, waits = w->stats.waits;
        CHECK(w->scheduler.visible && !w->scheduler.occluded);
        CHECK(ui_scheduler_wait(&w->scheduler, start) == INFINITE);
        CHECK(ui_window_step(w, duration));
        quiet = GetTickCount64() - start >= duration - 20 &&
            h->frames == frames && w->stats.timeouts == timeouts + 1;
        CHECK(w->stats.waits == waits + 1 && w->stats.last_wait_ms == duration);
    }
    CHECK(quiet);
    printf("idle trace: %lu ms, 1 blocking wait, 0 frames, 0 periodic timers\n", (unsigned long)duration);
}

int main(int argc, char **argv)
{
    CHECK(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    CHECK(SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)));
    static unsigned char memory[16384];
    Arena arena = {0}; Metrics metrics; MetricsArena binding = {0}; Harness h = {0};
    CHECK(metrics_init(&metrics)); CHECK(arena_init(&arena, memory, sizeof(memory)));
    CHECK(metrics_attach_arena(&metrics, &arena, &binding));
    CHECK(event_queue_create(&arena, 64, &h.queue) == CORE_OK);
    UiWindowConfig config = {0};
    config.instance = GetModuleHandleW(NULL); config.title = L"spoti-c window test";
    config.width = 800; config.height = 600; config.workers = h.queue;
    config.metrics = &metrics; config.frame = frame; config.worker = worker;
    config.device = device; config.context = &h;
    CHECK(ui_window_create(&h.window, &config));
    hit_tests(); geometry(&h);
    idle_trace(&h, argc == 2 && strcmp(argv[1], "--bench") == 0 ? 3000u : 100u);
    ui_window_set_occluded(&h.window, true);
    CHECK(ui_scheduler_wait(&h.window.scheduler, GetTickCount64()) == INFINITE);
    uint64_t frames = h.frames;
    HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, producer, &h, 0, NULL);
    CHECK(thread != NULL);
    uint64_t end = GetTickCount64() + 10000;
    while (atomic_load(&h.delivered) < 2000) {
        CHECK(GetTickCount64() < end);
        CHECK(ui_window_step(&h.window, 50));
    }
    CHECK(WaitForSingleObject(thread, 1000) == WAIT_OBJECT_0); CHECK(CloseHandle(thread));
    CHECK(h.frames == frames && h.window.stats.worker_events == 2000);
    /* Post after empty/reset but before wait: the manual event must remain set. */
    CHECK(ui_window_step(&h.window, 0));
    Event event = {0}; event.type = UI_DIRTY_STATE;
    CHECK(event_post(h.queue, &event) == CORE_OK);
    CHECK(WaitForSingleObject(event_wait_handle(h.queue), 0) == WAIT_OBJECT_0);
    CHECK(ui_window_step(&h.window, 0));
    CHECK(atomic_load(&h.delivered) == 2001);
    CHECK(WaitForSingleObject(event_wait_handle(h.queue), 0) == WAIT_TIMEOUT);
    ui_window_set_occluded(&h.window, false);
    CHECK(ui_window_step(&h.window, 0)); CHECK(h.frames == frames + 1);
    /* A signaled worker event wakes the native message wait, even with no
     * render deadline. Keep the accepted event queued until the next drain. */
    CHECK(event_post(h.queue, &event) == CORE_OK);
    HANDLE wake = event_wait_handle(h.queue);
    CHECK(MsgWaitForMultipleObjectsEx(1, &wake, INFINITE, QS_ALLINPUT,
        MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0);
    CHECK(ui_window_step(&h.window, 0));
    CHECK(atomic_load(&h.delivered) == 2002);
    CHECK(event_queue_close(h.queue) == CORE_OK);
    CHECK(!ui_window_step(&h.window, 0));
    CHECK(ui_window_destroy(&h.window)); CHECK(event_queue_destroy(h.queue) == CORE_OK);
    CHECK(event_queue_create(&arena, 2, &h.queue) == CORE_OK); config.workers = h.queue;
    CHECK(ui_window_create(&h.window, &config));
    SendMessageW(h.window.hwnd, WM_CLOSE, 0, 0);
    CHECK(!ui_window_step(&h.window, 0)); CHECK(ui_window_destroy(&h.window));
    CHECK(event_queue_close(h.queue) == CORE_OK); CHECK(event_queue_destroy(h.queue) == CORE_OK);
    arena_dispose(&arena); CHECK(metrics_balanced(&metrics));
    CoUninitialize();
    puts("ui_window: geometry, DPI, idle wait, worker/reset race and balanced shutdown passed");
    return 0;
}
