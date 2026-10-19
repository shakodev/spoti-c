#include "window.h"

static void frame(void *context, UiWindow *window, uint32_t reasons)
{
    (void)context; (void)reasons;
    /* Placeholder until the DX11 renderer lands. No continuous Present. */
    RECT rect; GetClientRect(window->hwnd, &rect);
    HDC dc = GetDC(window->hwnd);
    if (!dc) { return; }
    FillRect(dc, &rect, GetStockObject(BLACK_BRUSH));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(240, 240, 240));
    rect.left += MulDiv(40, (int)window->dpi, 96);
    DrawTextW(dc, L"spoti-c", -1, &rect, DT_LEFT | DT_SINGLELINE | DT_TOP);
    ReleaseDC(window->hwnd, dc);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    (void)previous; (void)command; (void)show;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) { return 1; }
    static unsigned char storage[8192];
    Arena arena = {0}; EventQueue *queue = NULL; UiWindow window = {0};
    if (!arena_init(&arena, storage, sizeof(storage))) { return 1; }
    if (event_queue_create(&arena, 32, &queue) != CORE_OK) { arena_dispose(&arena); return 1; }
    UiWindowConfig config = {0};
    config.instance = instance; config.title = L"spoti-c";
    config.width = 1100; config.height = 720; config.show = true;
    config.workers = queue; config.frame = frame;
    bool created = ui_window_create(&window, &config);
    if (created) { while (ui_window_step(&window, INFINITE)) { } }
    bool clean = !created || ui_window_destroy(&window);
    (void)event_queue_close(queue);
    Event events[32]; size_t count;
    (void)event_drain(queue, events, 32, &count);
    clean = event_queue_destroy(queue) == CORE_OK && clean;
    arena_dispose(&arena);
    return created && clean && !window.failed ? 0 : 1;
}
