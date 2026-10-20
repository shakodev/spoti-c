#include "app_artwork_host.h"
#include "ui_scheduler.h"
#include <string.h>

static void completed(void *context)
{
    Event event = {.type=UI_DIRTY_STATE};
    /* FULL already has queued work to wake the owner; no retry/polling loop. */
    (void)event_post(context,&event);
}
static DWORD remaining(ULONGLONG started, DWORD timeout)
{
    if (timeout == INFINITE) return INFINITE;
    ULONGLONG elapsed = GetTickCount64()-started;
    return elapsed >= timeout ? 0 : timeout-(DWORD)elapsed;
}
bool app_artwork_open(AppArtworkHost *h, Arena *core, UiRenderer *renderer,
    EventQueue *events, Metrics *metrics)
{
    if (!h || h->owner || !core || !core->base || !core->generation || core->capacity > MEMORY_BUDGET_CORE ||
        !renderer || !events || !event_wait_handle(events)) return false;
    h->owner = GetCurrentThreadId(); h->core = core; h->core_mark = arena_mark(core);
    h->core_end = core->used;
    if (!arena_init(&h->staging_arena,h->staging,sizeof(h->staging))) goto fail;
    h->staging_ready = true;
    if (metrics && !metrics_attach_arena(metrics,&h->staging_arena,&h->staging_metrics)) goto fail;
    void *decode = arena_alloc(&h->staging_arena,ART_DECODE_BYTES,16);
    unsigned char *upload = arena_alloc(&h->staging_arena,ART_UPLOAD_BYTES,16);
    void *storage = arena_alloc(core,artwork_size(),artwork_alignment());
    h->core_end = core->used;
    if (!decode || !upload || !storage) goto fail;
    CoreResult started = worker_start(core,ART_REQUESTS,(CoreClock){0},&h->worker);
    h->core_end = core->used;
    if (started != CORE_OK) goto fail;
    ArtworkConfig config = {.worker=h->worker,.fetch=artwork_http_fetch,.fetch_context=metrics,
        .gpu=artwork_renderer_gpu(renderer),.decode_workspace=decode,.upload=upload,
        .metrics=metrics,.wake=completed,.wake_context=events};
    if (artwork_init(storage,artwork_size(),&config,&h->art) != ART_OK) goto fail;
    h->ready = true; return true;
fail:
    (void)app_artwork_close(h,1000);
    return false;
}
Artwork *app_artwork_service(AppArtworkHost *h)
{
    return h && h->owner == GetCurrentThreadId() && h->ready && !h->closing ? h->art : NULL;
}
bool app_artwork_close(AppArtworkHost *h, DWORD timeout)
{
    if (!h) return false;
    if (!h->owner) return true;
    if (h->owner != GetCurrentThreadId()) return false;
    ULONGLONG started = GetTickCount64();
    h->ready = false; h->closing = true;
    if (h->art) {
        ArtResult result = artwork_stop(h->art);
        if (result == ART_OK) h->art = NULL;
        else if (result != ART_PENDING) return false;
    }
    if (h->worker) {
        if (worker_stop(h->worker) != CORE_OK ||
            worker_join(h->worker,remaining(started,timeout)) != CORE_OK) return false;
        h->worker = NULL;
    }
    if (h->art) {
        if (artwork_stop(h->art) != ART_OK) return false;
        h->art = NULL;
    }
    if (h->staging_ready) arena_dispose(&h->staging_arena);
    if (h->core && h->core->generation == h->core_mark.generation && h->core->used == h->core_end) {
        if (!arena_rewind(h->core,h->core_mark)) return false;
    }
    memset(h,0,offsetof(AppArtworkHost,staging));
    return true;
}
