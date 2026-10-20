#include "headless/fixture_server.h"
#include "headless/headless.h"
#include "app_desktop.h"
#include "app_fixture.h"
#include "benchmark.h"
#include "app_artwork_host.h"
#include <windowsx.h>
#include <wchar.h>
#include <stdio.h>
#include <string.h>

#define WIDE_INNER(s) L##s
#define WIDE(s) WIDE_INNER(s)

static AppDesktop desktop;
static AppFixture fixtures;
static HeadlessServer server={.listener=INVALID_SOCKET,.peer=INVALID_SOCKET,.file=INVALID_HANDLE_VALUE};
static Headless *playback;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES], storage[MEMORY_BUDGET_CORE];
static Startup startup;
static Arena arena;
static EventQueue *queue;
static bool demo, wasapi, probe;
static StartupTimes *probe_times;
static BenchProbe benchmark;
static AppArtworkHost artwork_host;

static bool input(void *context,UiWindow *window,UINT message,WPARAM wp,LPARAM lp,LRESULT *out)
{
    if (bench_probe_input(&benchmark,&desktop,window,playback,message,wp,out)) { return true; }
    return app_desktop_input(context,window,message,wp,lp,out);
}

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
static PlayerResult demo_refresh(void *context)
{
    return headless_refresh_clock(((AppFixture *)context)->player_context);
}
static PlayerResult demo_queue(void *context, const PlaylistQueue *items)
{
    AppFixture *h=context; h->last_queue=*items;
    PlayerCommand command={.type=PLAYER_TRANSFER,.transfer=&items->transfer}; return headless_submit(h->player_context,&command);
}
static void deferred(UiWindow *window)
{
    if (!startup.times.ticks[STARTUP_PRESENT]) { return; }
    if (!startup.services_started) {
        startup.services_started=true;
        if (!probe && startup.path[0]) {
            wchar_t directory[240]; wcscpy_s(directory,240,startup.path); wchar_t *slash=wcsrchr(directory,L'/');
            if (slash) { *slash=0; (void)CreateDirectoryW(directory,NULL); }
        }
        (void)startup_writer_start(&startup,&arena);
        if (demo && !probe && app_fixture_init(&fixtures,&arena,queue,&desktop.app,WIDE(APP_FIXTURE_DIR))==APP_OK) {
            desktop.fixture=&fixtures; AppServices services=app_fixture_services(&fixtures);
            (void)SetWindowTextW(window->hwnd,L"spoti-c | Offline demo");
            (void)CreateDirectoryW(L".cache",NULL); (void)CreateDirectoryW(L".cache/app-demo",NULL);
            if (headless_server_open(&server,WIDE(APP_AUDIO_FIXTURE))) {
                char url[128]; (void)snprintf(url,sizeof(url),"http://127.0.0.1:%u/audio",server.port);
                HeadlessConfig hc={.cache_directory=L".cache/app-demo",.fixture_url=url,.realtime=true,
                    .wasapi=wasapi,.state_changed=player_wake,.state_context=queue};
                if (headless_open(&hc,&playback)) {
                    fixtures.player_context=playback; services.submit=demo_submit; services.snapshot=demo_snapshot;
                    services.refresh_clock=demo_refresh; services.playlist_queue=demo_queue;
                }
            }
            desktop.app.services=services; (void)app_session(&desktop.app,true,true,GetTickCount64());
        }
        if (!probe && !benchmark.shared && app_artwork_open(&artwork_host,&arena,&desktop.surface.renderer,queue,NULL)) {
            desktop.app.services.artwork=app_artwork_service(&artwork_host);
            (void)artwork_navigate(desktop.app.services.artwork,desktop.app.model.generation);
        }
        if (!probe) { (void)app_desktop_media_open(&desktop,window); }
        startup_mark(&startup.times,STARTUP_SERVICES);
    }
    if (!demo && desktop.app.model.logged_in && desktop.app.model.online) { startup_mark(&startup.times,STARTUP_ONLINE); }
    if (probe) {
        if (probe_times) { *probe_times=startup.times; }
        PostMessageW(window->hwnd,WM_CLOSE,0,0); return;
    }
    static uint64_t saved_at;
    if (GetTickCount64()-saved_at>=2000) {
        startup_capture(&startup.captured,&desktop.app,window->hwnd);
        bool busy; StateCacheResult result=startup_save_status(&startup,&busy);
        if (!busy && (result!=STATE_CACHE_OK || memcmp(&startup.captured,&startup.pending,sizeof(StateCache))) &&
            startup_save(&startup,&startup.captured)==STATE_CACHE_OK) {
            startup.restored=startup.captured;
            if (desktop.app.model.logged_in) { desktop.app.cached=&startup.restored; }
            saved_at=GetTickCount64();
        }
    }
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    LARGE_INTEGER entered; QueryPerformanceCounter(&entered);
    (void)previous; (void)show;
    wchar_t path[240]={0};
    probe=wcsstr(command,L"--startup-probe=")!=NULL; demo=wcsstr(command,L"--demo")!=NULL; wasapi=wcsstr(command,L"--demo-wasapi")!=NULL;
    HANDLE mapping=NULL;
    const wchar_t *benchmark_name=wcsstr(command,L"--benchmark=");
    if (benchmark_name && !bench_probe_open(&benchmark,benchmark_name+12)) { return 1; }
    if (probe) {
        const wchar_t *name=wcsstr(command,L"--startup-probe=")+16;
        mapping=OpenFileMappingW(FILE_MAP_WRITE,FALSE,name);
        if (!mapping) { return 1; }
        probe_times=MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,sizeof(StartupTimes));
        wcscpy_s(path,240,L".cache/startup-bench.state");
    } else if (benchmark.shared) {
        wcscpy_s(path,240,L".cache/release-bench.state");
    } else {
        DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,200);
        if (n && n<200) { wcscat_s(path,240,L"/spoti-c/shell.state"); }
        else { path[0]=0; }
    }
    startup_begin(&startup,path); startup.times.ticks[STARTUP_MAIN]=(uint64_t)entered.QuadPart;
    desktop.startup=&startup; desktop.startup_prepare=deferred;
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) { return 1; }
    UiWindow window = {0}; bool clean = true;
    if (!arena_init(&arena,storage,sizeof(storage))) { return 1; }
    if (benchmark.shared && !metrics_attach_arena(&benchmark.metrics,&arena,&benchmark.binding)) { return 1; }
    if (event_queue_create(&arena,32,&queue) != CORE_OK) { arena_dispose(&arena); return 1; }
    UiWindowConfig config = {.instance=instance,.title=L"spoti-c",.width=1100,.height=720,
        .workers=queue,.frame=app_desktop_frame,.prepare=app_desktop_prepare,.input=input,.context=&desktop,
        .metrics=benchmark.shared ? &benchmark.metrics : NULL};
    bool created = ui_window_create(&window,&config);
    if (created) { startup_mark(&startup.times,STARTUP_WINDOW); startup_place(window.hwnd,&startup.restored); }
    if (created && ui_surface_create(&desktop.surface,&window,false) == UI_RENDER_OK) {
        AppServices services = {0};
        if (app_init(&desktop.app,&services,&window.scheduler,clay,sizeof(clay),scratch,sizeof(scratch)) == APP_OK) {
            startup_restore(&desktop.app,&startup.restored);
            desktop.surface.app=&desktop.app;
            /* Compact labels may trim. FONT_FACE_NOTICE explicitly bypasses
             * this policy in both Clay measurement and glyph rendering. */
            desktop.surface.text.config.weight=400; desktop.surface.text.config.max_width=260; desktop.surface.text.config.ellipsis=true;
            app_bind_fonts(&desktop.app,&desktop.surface.text);
            ShowWindow(window.hwnd,SW_SHOWNORMAL);
            while (ui_window_step(&window,INFINITE)) { }
        } else { clean=false; }
    } else if (created) { ShowWindow(window.hwnd,SW_SHOWNORMAL); while (ui_window_step(&window,INFINITE)) { } }
    if (startup.worker) {
        if (!probe && desktop.app.alive) {
            if (WaitForSingleObject(worker_idle_handle(startup.worker),10000)!=WAIT_OBJECT_0) { return 1; }
            startup_capture(&startup.captured,&desktop.app,window.hwnd);
            if (startup_save(&startup,&startup.captured)!=STATE_CACHE_OK) { clean=false; }
        }
        if (startup_writer_close(&startup,10000)!=STATE_CACHE_OK && !probe) { clean=false; }
    }
    if (!app_desktop_media_close(&desktop)) { return 1; }
    if (!app_artwork_close(&artwork_host,10000)) { return 1; }
    desktop.app.services.artwork=NULL;
    /* Font binding borrows the Clay arena: unbind before disposing App. */
    ui_surface_destroy(&desktop.surface); app_dispose(&desktop.app);
    if (desktop.fixture && !app_fixture_stop(desktop.fixture)) { return 1; }
    if (playback) { HeadlessMetrics metrics; if (!headless_close(playback,&metrics)) { return 1; } }
    if (!headless_server_close(&server)) { return 1; }
    clean=(!created||ui_window_destroy(&window))&&clean;
    (void)event_queue_close(queue); Event events[32]; size_t count;
    (void)event_drain(queue,events,32,&count); clean=event_queue_destroy(queue)==CORE_OK&&clean;
    arena_dispose(&arena);
    if (benchmark.shared) { bench_probe_close(&benchmark,clean&&metrics_balanced(&benchmark.metrics)); }
    if (probe_times) { UnmapViewOfFile(probe_times); } if (mapping) { CloseHandle(mapping); }
    return created&&clean&&!window.failed ? 0 : 1;
}
