#include "headless/fixture_server.h"
#include "headless/headless.h"
#include "app_desktop.h"
#include "app_fixture.h"
#include <windowsx.h>
#include <wchar.h>
#include <stdio.h>

#define WIDE_INNER(s) L##s
#define WIDE(s) WIDE_INNER(s)

static AppDesktop desktop;
static AppFixture fixtures;
static HeadlessServer server={.listener=INVALID_SOCKET,.peer=INVALID_SOCKET,.file=INVALID_HANDLE_VALUE};
static Headless *playback;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES], storage[MEMORY_BUDGET_CORE];

static void player_wake(void *context)
{
    Event event = {.type=UI_DIRTY_STATE}; (void)event_post(context,&event);
}
static PlayerResult demo_submit(void *context, const PlayerCommand *c)
{
    AppFixture *h = context; return headless_submit(h->player_context,c);
}
static PlayerSnapshot demo_snapshot(void *context)
{
    AppFixture *h = context; return headless_snapshot(h->player_context);
}
static PlayerResult demo_queue(void *context, const PlaylistQueue *queue)
{
    AppFixture *h=context; h->last_queue=*queue;
    PlayerCommand command={.type=PLAYER_TRANSFER,.transfer=&queue->transfer}; return headless_submit(h->player_context,&command);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    (void)previous; (void)show;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) { return 1; }
    Arena arena = {0}; EventQueue *queue = NULL; UiWindow window = {0}; bool clean = true;
    if (!arena_init(&arena,storage,sizeof(storage))) { return 1; }
    if (event_queue_create(&arena,32,&queue) != CORE_OK) { arena_dispose(&arena); return 1; }
    UiWindowConfig config = {.instance=instance,.title=L"spoti-c",.width=1100,.height=720,
        .workers=queue,.frame=app_desktop_frame,.prepare=app_desktop_prepare,.input=app_desktop_input,.context=&desktop};
    bool created = ui_window_create(&window,&config);
    if (created && ui_surface_create(&desktop.surface,&window,false) == UI_RENDER_OK) {
        AppServices services = {0};
        if (wcsstr(command,L"--demo")) {
            if (app_fixture_init(&fixtures,&arena,queue,&desktop.app,WIDE(APP_FIXTURE_DIR)) == APP_OK) {
                desktop.fixture=&fixtures; services=app_fixture_services(&fixtures);
                (void)SetWindowTextW(window.hwnd,L"spoti-c | Offline demo");
                (void)CreateDirectoryW(L".cache",NULL); (void)CreateDirectoryW(L".cache/app-demo",NULL);
                if (headless_server_open(&server,WIDE(APP_AUDIO_FIXTURE))) {
                    char url[128]; (void)snprintf(url,sizeof(url),"http://127.0.0.1:%u/audio",server.port);
                    HeadlessConfig hc = {.cache_directory=L".cache/app-demo",.fixture_url=url,.realtime=true,
                        .wasapi=wcsstr(command,L"--demo-wasapi")!=NULL,.state_changed=player_wake,.state_context=queue};
                    if (headless_open(&hc,&playback)) {
                        fixtures.player_context=playback; services.submit=demo_submit; services.snapshot=demo_snapshot; services.playlist_queue=demo_queue;
                    }
                }
            }
        }
        if (app_init(&desktop.app,&services,&window.scheduler,clay,sizeof(clay),scratch,sizeof(scratch)) == APP_OK) {
            desktop.surface.app=&desktop.app;
            desktop.surface.text.config.weight=400; desktop.surface.text.config.max_width=260; desktop.surface.text.config.ellipsis=true;
            app_bind_fonts(&desktop.app,&desktop.surface.text);
            if (desktop.fixture) { (void)app_session(&desktop.app,true,true,GetTickCount64()); }
            ShowWindow(window.hwnd,SW_SHOWNORMAL);
            while (ui_window_step(&window,INFINITE)) { }
        } else { clean=false; }
    } else if (created) { ShowWindow(window.hwnd,SW_SHOWNORMAL); while (ui_window_step(&window,INFINITE)) { } }
    /* Font binding borrows the Clay arena: unbind before disposing App. */
    ui_surface_destroy(&desktop.surface); app_dispose(&desktop.app);
    if (desktop.fixture && !app_fixture_stop(desktop.fixture)) { return 1; }
    if (playback) { HeadlessMetrics metrics; if (!headless_close(playback,&metrics)) { return 1; } }
    if (!headless_server_close(&server)) { return 1; }
    clean=(!created||ui_window_destroy(&window))&&clean;
    (void)event_queue_close(queue); Event events[32]; size_t count;
    (void)event_drain(queue,events,32,&count); clean=event_queue_destroy(queue)==CORE_OK&&clean;
    arena_dispose(&arena); return created&&clean&&!window.failed ? 0 : 1;
}
