#include "playlist_fixture.h"
#include "ui/app.h"
#include "ui/screens/playlist.h"
#include <stdio.h>
#include <string.h>

static TrackFilter filter;
static Playlist playlist;
static App app;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES], core_storage[MEMORY_BUDGET_CORE];
static double elapsed(LARGE_INTEGER start, LARGE_INTEGER frequency)
{
    LARGE_INTEGER now; CHECK(QueryPerformanceCounter(&now));
    return (double)(now.QuadPart-start.QuadPart)*1000/(double)frequency.QuadPart;
}
static void sort(double *values, unsigned count)
{
    for (unsigned i=1;i<count;++i) {
        double v=values[i]; unsigned at=i; while (at && values[at-1]>v) { values[at]=values[at-1]; --at; } values[at]=v;
    }
}
static DWORD WINAPI run(void *context)
{
    (void)context;
    TrackDisk disk; wchar_t path[MAX_PATH]; playlist_fixture_disk(&disk,path,10000);
    Arena core={0}; Worker *worker=NULL; EventQueue *events=NULL;
    CHECK(arena_init(&core,core_storage,sizeof(core_storage)) && event_queue_create(&core,16,&events)==CORE_OK);
    CHECK(worker_start(&core,4,(CoreClock){0},&worker)==CORE_OK);
    CHECK(track_filter_init(&filter,worker,events)==TRACK_OK); playlist_init(&playlist,&filter);
    AppServices services={.playlist=&playlist};
    CHECK(app_init(&app,&services,NULL,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    app.model.logged_in=true; app.model.location.screen=APP_PLAYLIST;
    CHECK(playlist_route(&playlist,disk.source.uri,app.model.generation)==TRACK_PENDING);
    CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING);
    (void)app_layout(&app,1100,720,96,(UiWidgetInput){0});
    for (unsigned i=0;i<100;++i) {
        (void)app_update(&app,0); (void)app_layout(&app,1100,720,96,(UiWidgetInput){0});
        if (!playlist.pending && atomic_load(&filter.phase)==0) { break; }
        (void)WaitForSingleObject(event_wait_handle(events),100); Event batch[16]; size_t n; (void)event_drain(events,batch,16,&n);
    }
    CHECK(playlist.ready && playlist.count==10000 && playlist.row_count>0 && !atomic_load(&filter.phase));
    LARGE_INTEGER frequency,start; CHECK(QueryPerformanceFrequency(&frequency));
    double samples[40];
    for (unsigned i=0;i<40;++i) {
        filter.job=(TrackJob){.source=disk.source,.generation=i+1,.query="sOnG 0",.sort=TRACK_SOURCE_ORDER};
        CHECK(QueryPerformanceCounter(&start)); CHECK(track_filter_execute(&filter,NULL)==TRACK_OK); samples[i]=elapsed(start,frequency);
        CHECK(filter.job.count==9999 && filter.job.coverage==TRACK_FULL_INDEX); /* Unicode replacement at 777. */
    }
    sort(samples,40); double median=samples[20],p95=samples[38];
    filter.job.sort=TRACK_TITLE_ORDER; CHECK(QueryPerformanceCounter(&start));
    CHECK(track_filter_execute(&filter,NULL)==TRACK_OK); double initial_sort_ms=elapsed(start,frequency);
    double sorted[40];
    for (unsigned i=0;i<40;++i) {
        CHECK(QueryPerformanceCounter(&start)); CHECK(track_filter_execute(&filter,NULL)==TRACK_OK); sorted[i]=elapsed(start,frequency);
    }
    sort(sorted,40);
    CHECK(QueryPerformanceCounter(&start));
    for (unsigned i=0;i<10000;++i) {
        Clay_RenderCommandArray commands=app_layout(&app,1100,720,96,(UiWidgetInput){.x=-1,.y=-1});
        CHECK(commands.length && app.widgets.error==UI_WIDGET_OK && playlist.rendered_rows<=16);
    }
    double layout_ms=elapsed(start,frequency)/10000;
    printf("bench_tracklist: source=10k immutable disk records (OS cache warm), UTF-8 validation + substring + ID/map publication\n");
    printf("filter median=%.3f ms p95=%.3f ms; Clay layout=%.3f ms/frame; rows=%u; models=%zu bytes; app=%zu bytes\n",
        median,p95,layout_ms,playlist.rendered_rows,sizeof(Playlist)+sizeof(TrackFilter)+COLLECTION_MODEL_BUDGET,sizeof(App));
    printf("initial title order=%.3f ms (worker); cached-title filter median=%.3f ms p95=%.3f ms\n",initial_sort_ms,sorted[20],sorted[38]);
    CHECK(p95<=16 && sorted[38]<=16 && layout_ms<=16);
    app_dispose(&app); CHECK(worker_stop(worker)==CORE_OK && worker_join(worker,5000)==CORE_OK);
    CHECK(track_disk_close(&disk) && DeleteFileW(path));
    CHECK(event_queue_close(events)==CORE_OK); Event batch[16]; size_t n; (void)event_drain(events,batch,16,&n);
    CHECK(event_queue_destroy(events)==CORE_OK); arena_dispose(&core); return 0;
}
int main(void)
{
    HANDLE thread=CreateThread(NULL,0,run,NULL,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread,55000)==WAIT_OBJECT_0); DWORD result;
    CHECK(GetExitCodeThread(thread,&result) && CloseHandle(thread)); return (int)result;
}
