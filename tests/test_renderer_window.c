#include "ui/ui_surface.h"
#include "support/test_check.h"
#include <string.h>

static UiSurface surface;

int main(int argc, char **argv)
{
    CHECK(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    static unsigned char memory[8192]; Arena arena = {0}; EventQueue *queue = NULL;
    UiWindow window = {0};
    CHECK(arena_init(&arena,memory,sizeof(memory)));
    CHECK(event_queue_create(&arena,16,&queue) == CORE_OK);
    UiWindowConfig config = {0};
    config.instance = GetModuleHandleW(NULL); config.title = L"spoti-c DX11 test";
    config.width = 800; config.height = 600; config.workers = queue;
    config.frame = ui_surface_frame; config.prepare = ui_surface_prepare; config.context = &surface;
    CHECK(ui_window_create(&window,&config));
    CHECK(SetWindowPos(window.hwnd,NULL,-20000,-20000,800,600,SWP_NOZORDER | SWP_NOACTIVATE));
    UiRenderResult result = ui_surface_create(&surface,&window,true);
    if (result == UI_RENDER_UNSUPPORTED) {
        fprintf(stderr,"SKIP: WARP HWND swapchain unavailable\n");
        CHECK(ui_window_destroy(&window)); CHECK(event_queue_close(queue) == CORE_OK);
        CHECK(event_queue_destroy(queue) == CORE_OK); arena_dispose(&arena); return 77;
    }
    CHECK(result == UI_RENDER_OK);
    ShowWindow(window.hwnd,SW_SHOWNOACTIVATE);
    for (unsigned i = 0; i < 20; ++i) { CHECK(ui_window_step(&window,0)); }
    CHECK(surface.ready && ui_renderer_stats(&surface.renderer).presents > 0);
    CHECK(surface.last_result == UI_RENDER_OK || surface.last_result == UI_RENDER_OCCLUDED);
    CHECK(SetWindowPos(window.hwnd,NULL,0,0,900,650,SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE));
    for (unsigned i = 0; i < 10; ++i) { CHECK(ui_window_step(&window,0)); }
    RECT rect; CHECK(GetClientRect(window.hwnd,&rect));
    CHECK(surface.renderer.config.width == (uint32_t)rect.right);
    CHECK(surface.renderer.config.height == (uint32_t)rect.bottom);
    ui_window_set_occluded(&window,true);
    CHECK(PostMessageW(window.hwnd,UI_WINDOW_OCCLUSION_MESSAGE,0,0));
    uint64_t presents = ui_renderer_stats(&surface.renderer).presents;
    CHECK(ui_window_step(&window,0));
    CHECK(ui_renderer_stats(&surface.renderer).presents > presents);
    /* Inject the removed-device state into the same production owner path. */
    ui_renderer_invalidate(&surface.renderer);
    ui_scheduler_dirty(&window.scheduler,UI_DIRTY_DEVICE); ui_window_set_occluded(&window,false);
    CHECK(ui_window_step(&window,0)); CHECK(surface.recovery_pending);
    CHECK(ui_window_step(&window,0)); CHECK(!surface.recovery_pending && surface.ready);
    CHECK(surface.recovery_attempted);
    for (unsigned i = 0; i < 10; ++i) { CHECK(ui_window_step(&window,0)); }
    uint32_t duration = argc == 2 && strcmp(argv[1],"--bench") == 0 ? 3000u : 100u;
    bool quiet = false;
    for (unsigned i = 0; i < 20 && !quiet; ++i) {
        uint64_t start = GetTickCount64(), timeouts = window.stats.timeouts;
        UiRendererStats before = ui_renderer_stats(&surface.renderer);
        CHECK(ui_window_step(&window,duration));
        UiRendererStats after = ui_renderer_stats(&surface.renderer);
        quiet = window.stats.timeouts == timeouts + 1 && GetTickCount64() - start >= duration - 20 &&
            after.presents == before.presents && after.maps == before.maps && after.draw_calls == before.draw_calls;
    }
    CHECK(quiet);
    printf("DX11 idle trace: %lu ms, 1 blocking wait, 0 Present, 0 Map, 0 Draw\n",(unsigned long)duration);
    /* A second persistent removal takes the bounded fallback path. */
    ui_renderer_invalidate(&surface.renderer);
    ui_scheduler_dirty(&window.scheduler,UI_DIRTY_DEVICE); ui_window_set_occluded(&window,false);
    CHECK(ui_window_step(&window,0)); CHECK(ui_window_step(&window,0));
    CHECK(!surface.ready && ui_renderer_stats(&surface.renderer).resources == 0);
    ui_surface_destroy(&surface); CHECK(ui_window_destroy(&window));
    CHECK(event_queue_close(queue) == CORE_OK); CHECK(event_queue_destroy(queue) == CORE_OK);
    arena_dispose(&arena);
    puts("DX11 window: present, resize, notification, recovery, fallback and shutdown passed");
    return 0;
}
