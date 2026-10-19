#include "artwork.h"
#include <string.h>
#include <wchar.h>
typedef struct ArtRequest {
    ArtworkId id;
    wchar_t url[ART_URL_CHARS];
    bool pending;
} ArtRequest;
struct Artwork {
    ArtworkConfig config;
    TextureCache cache;
    ArtRequest requests[ART_REQUESTS];
    ArtworkStats stats;
    SRWLOCK lock;
    HANDLE wake;
    MetricsHandle wake_metric;
    uint64_t generation, ticket;
    DWORD owner;
    unsigned active;
    bool busy, done, stopping, upload_pending;
    ArtResult result;
    uint32_t width, height;
    ImageStats decoded;
};
size_t artwork_size(void) { return sizeof(Artwork); }
size_t artwork_alignment(void) { return _Alignof(Artwork); }
static bool owner(const Artwork *a) { return a && a->owner == GetCurrentThreadId() && a->wake; }
static void run(const Event *event, const CancelToken *cancel, void *ctx)
{
    Artwork *a = ctx;
    ArtRequest *request = &a->requests[a->active];
    size_t bytes = 0;
    (void)event;
    a->decoded = (ImageStats){0};
    a->result = a->config.fetch(a->config.fetch_context, request->url,
        a->config.decode_workspace, ART_COMPRESSED_BYTES, &bytes, cancel);
    if (a->result == ART_OK && bytes > ART_COMPRESSED_BYTES) a->result = ART_TOO_LARGE;
    if (a->result == ART_OK && worker_cancelled(cancel)) a->result = ART_CANCELLED;
    if (a->result == ART_OK) a->result = image_decode(a->config.decode_workspace,
        a->config.decode_workspace, bytes, a->config.upload, request->id.size, cancel,
        &a->width, &a->height, &a->decoded);
}
static void complete(const Event *event, WorkerOutcome outcome, void *ctx)
{
    Artwork *a = ctx;
    (void)event;
    AcquireSRWLockExclusive(&a->lock);
    if (outcome != WORKER_DONE) a->result = ART_CANCELLED;
    a->done = true;
    SetEvent(a->wake);
    ReleaseSRWLockExclusive(&a->lock);
}
ArtResult artwork_init(void *storage, size_t bytes, const ArtworkConfig *cfg, Artwork **out)
{
    if (out) *out = NULL;
    if (!out || !storage || (uintptr_t)storage % _Alignof(Artwork) || bytes < sizeof(Artwork) ||
        !cfg || !cfg->worker || !cfg->fetch || !cfg->decode_workspace ||
        ((uintptr_t)cfg->decode_workspace & 15u) || !cfg->upload ||
        !cfg->gpu.upload || !cfg->gpu.release || !cfg->gpu.valid) return ART_INVALID;
    Artwork *a = storage;
    *a = (Artwork){.config = *cfg, .owner = GetCurrentThreadId()};
    InitializeSRWLock(&a->lock);
    a->wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!a->wake) return ART_IO_ERROR;
    if (cfg->metrics && !metrics_handle_acquire(cfg->metrics, &a->wake_metric, (uintptr_t)a->wake)) {
        CloseHandle(a->wake); a->wake = NULL; return ART_BUDGET;
    }
    texture_cache_init(&a->cache, cfg->gpu);
    *out = a; return ART_OK;
}
ArtResult artwork_navigate(Artwork *a, uint64_t generation)
{
    if (!owner(a) || a->stopping || !generation) return ART_INVALID;
    if (a->generation == generation) return ART_OK;
    if (generation < a->generation) return ART_STALE;
    a->generation = generation;
    a->upload_pending = false;
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) a->cache.entries[i].pinned = false;
    for (unsigned i = 0; i < ART_REQUESTS; ++i) {
        if (a->busy && a->active == i) continue;
        a->requests[i].pending = false;
    }
    if (a->busy && a->requests[a->active].id.generation != generation)
        (void)worker_cancel(a->config.worker, a->ticket);
    return ART_OK;
}
ArtResult artwork_begin_visible(Artwork *a)
{
    if (!owner(a) || a->stopping) return ART_INVALID;
    texture_cache_prepare(&a->cache);
    if (a->upload_pending) SetEvent(a->wake);
    return ART_OK;
}
ArtResult artwork_request(Artwork *a, ArtworkId id, const ArtworkSource *sources, size_t count)
{
    if (!owner(a) || a->stopping || !id.size || id.size > ART_MAX_EDGE || !sources || !count || count > 32)
        return ART_INVALID;
    if (!a->generation || a->generation != id.generation) return ART_STALE;
    ArtworkHandle handle;
    if (texture_cache_find(&a->cache, id, &handle)) return ART_OK;
    unsigned slot = ART_REQUESTS;
    for (unsigned i = 0; i < ART_REQUESTS; ++i) {
        if ((a->requests[i].pending || ((a->busy || a->upload_pending) && a->active == i)) && artwork_id_equal(a->requests[i].id, id))
            return ART_PENDING;
        if (!a->requests[i].pending && !((a->busy || a->upload_pending) && a->active == i) && slot == ART_REQUESTS) slot = i;
    }
    if (slot == ART_REQUESTS) return ART_FULL;
    size_t best = SIZE_MAX;
    uint32_t best_edge = 0;
    for (size_t i = 0; i < count; ++i) {
        uint32_t w = sources[i].width, h = sources[i].height, edge = w > h ? w : h;
        if (!sources[i].url || !w || !h || edge > ART_MAX_EDGE) continue;
        if (best == SIZE_MAX || (best_edge < id.size ? edge > best_edge : edge >= id.size && edge < best_edge)) {
            best = i; best_edge = edge;
        }
    }
    if (best == SIZE_MAX) return ART_UNSUPPORTED;
    const wchar_t *url = sources[best].url;
    size_t len = 0;
    while (len < ART_URL_CHARS && url[len]) ++len;
    if (len == ART_URL_CHARS || len < 9 || _wcsnicmp(url, L"https://", 8)) return ART_INVALID;
    a->requests[slot].id = id;
    memcpy(a->requests[slot].url, url, (len + 1) * sizeof(wchar_t));
    a->requests[slot].pending = true;
    return ART_PENDING;
}
ArtResult artwork_prepare(Artwork *a)
{
    if (!owner(a) || a->stopping) return ART_INVALID;
    if (!a->busy) ResetEvent(a->wake);
    texture_cache_collect(&a->cache);
    ArtResult result = ART_OK;
    if (a->busy) {
        AcquireSRWLockExclusive(&a->lock);
        bool done = a->done;
        if (done) { a->done = false; ResetEvent(a->wake); }
        ReleaseSRWLockExclusive(&a->lock);
        if (done) {
            ArtworkId id = a->requests[a->active].id;
            result = a->result;
            a->stats.decode = a->decoded;
            ++a->stats.completed;
            if (id.generation != a->generation) { ++a->stats.stale; result = ART_STALE; }
            else if (result == ART_OK) a->upload_pending = true;
            if (a->result == ART_CANCELLED) ++a->stats.cancelled;
            a->busy = false;
        }
    }
    if (a->upload_pending) {
        ArtworkHandle handle;
        result = texture_cache_insert(&a->cache, a->requests[a->active].id,
            a->width, a->height, a->config.upload, &handle);
        if (result == ART_OK) ++a->stats.uploaded;
        if (result != ART_FULL) a->upload_pending = false;
        else for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i)
            if (a->cache.entries[i].retired) { SetEvent(a->wake); break; }
    }
    if (!a->busy && !a->upload_pending) for (unsigned i = 0; i < ART_REQUESTS; ++i) if (a->requests[i].pending) {
        a->active = i;
        a->result = ART_CANCELLED; a->decoded = (ImageStats){0};
        WorkerJob job = {.run = run, .complete = complete, .context = a,
            .deadline = CORE_CLOCK_NEVER, .priority = WORKER_ART};
        if (worker_submit(a->config.worker, &job, &a->ticket) != CORE_OK) return ART_FULL;
        a->busy = true; a->requests[i].pending = false; ++a->stats.submitted;
        break;
    }
    return result;
}
bool artwork_lookup(Artwork *a, ArtworkId id, ArtworkHandle *out)
{
    return owner(a) && !a->stopping && id.generation == a->generation && texture_cache_find(&a->cache, id, out);
}
bool artwork_resolve(Artwork *a, ArtworkHandle h, UiTexture *out)
{
    if (!owner(a) || a->stopping || !h.slot || h.slot > ART_CACHE_SLOTS ||
        a->cache.entries[h.slot - 1].id.generation != a->generation) {
        if (out) *out = 0;
        return false;
    }
    return texture_cache_resolve(&a->cache, h, out);
}
HANDLE artwork_wait_handle(const Artwork *a) { return a ? a->wake : NULL; }
UiRenderResult artwork_draw(Artwork *a, ArtworkId id, UiRenderFrame *frame, UiRect box)
{
    ArtworkHandle handle;
    UiTexture texture = 0;
    bool ready = artwork_lookup(a, id, &handle) && artwork_resolve(a, handle, &texture);
    QuadInstance quad = {.box = {box.x, box.y, box.w, box.h}, .uv = {0, 0, 1, 1},
        .color = {1, 1, 1, 1}, .effect = {UI_QUAD_IMAGE, 0, 0, 0}};
    if (!ready) {
        quad.color[0] = quad.color[1] = quad.color[2] = 0.12f;
        quad.effect[0] = UI_QUAD_SOLID;
    }
    return ui_frame_quad(frame, &quad, texture);
}
ArtworkStats artwork_stats(Artwork *a)
{
    if (!owner(a)) return (ArtworkStats){0};
    ArtworkStats stats = a->stats;
    stats.active = a->busy || a->upload_pending;
    for (unsigned i = 0; i < ART_REQUESTS; ++i) stats.pending += a->requests[i].pending ? 1u : 0u;
    stats.gpu_bytes = a->cache.bytes; stats.peak_gpu_bytes = a->cache.peak_bytes;
    return stats;
}
ArtResult artwork_stop(Artwork *a)
{
    if (!owner(a)) return ART_INVALID;
    a->stopping = true;
    if (a->busy) {
        (void)worker_cancel(a->config.worker, a->ticket);
        AcquireSRWLockExclusive(&a->lock);
        bool done = a->done;
        ReleaseSRWLockExclusive(&a->lock);
        if (!done) return ART_PENDING;
        a->busy = false;
    }
    texture_cache_clear(&a->cache);
    if (!CloseHandle(a->wake)) return ART_IO_ERROR;
    if (a->wake_metric.owner) (void)metrics_handle_release(&a->wake_metric);
    a->wake = NULL;
    return ART_OK;
}
