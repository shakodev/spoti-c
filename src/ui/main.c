#include "ui_surface.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    (void)previous; (void)command; (void)show;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) { return 1; }
    static unsigned char storage[8192];
    static UiSurface surface;
    Arena arena = {0}; EventQueue *queue = NULL; UiWindow window = {0};
    if (!arena_init(&arena, storage, sizeof(storage))) { return 1; }
    if (event_queue_create(&arena, 32, &queue) != CORE_OK) { arena_dispose(&arena); return 1; }
    UiWindowConfig config = {0};
    config.instance = instance; config.title = L"spoti-c";
    config.width = 1100; config.height = 720;
    config.workers = queue; config.frame = ui_surface_frame; config.prepare = ui_surface_prepare;
    config.context = &surface;
    bool created = ui_window_create(&window, &config);
    if (created) {
        (void)ui_surface_create(&surface, &window, false);
        ShowWindow(window.hwnd, SW_SHOWNORMAL);
        while (ui_window_step(&window, INFINITE)) { }
    }
    ui_surface_destroy(&surface);
    bool clean = !created || ui_window_destroy(&window);
    (void)event_queue_close(queue);
    Event events[32]; size_t count;
    (void)event_drain(queue, events, 32, &count);
    clean = event_queue_destroy(queue) == CORE_OK && clean;
    arena_dispose(&arena);
    return created && clean && !window.failed ? 0 : 1;
}
