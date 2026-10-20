#include "ui/artwork.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include "artwork_fixtures.h"
#include <string.h>
#include <wchar.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static _Thread_local bool watch_heap;
static unsigned heap_calls;
static int __cdecl allocation_hook(int type, void *data, size_t size, int block,
    long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block; (void)request; (void)file; (void)line;
    if (watch_heap) { ++heap_calls; } return 1;
}
#endif

typedef struct FakeTexture { UiTexture id; uint32_t bytes; bool invalid; } FakeTexture;
typedef struct FakeGpu {
    FakeTexture textures[ART_CACHE_SLOTS];
    DWORD owner;
    uint32_t serial, bytes, peak, live, limit;
    uint64_t uploads, releases;
    ArtResult fail;
} FakeGpu;

static ArtResult upload(void *context, uint32_t width, uint32_t height,
    const unsigned char *rgba, UiTexture *out)
{
    FakeGpu *gpu = context;
    CHECK(GetCurrentThreadId() == gpu->owner && rgba != NULL);
    CHECK(width && height && width <= ART_MAX_EDGE && height <= ART_MAX_EDGE);
    if (gpu->fail != ART_OK) { return gpu->fail; }
    if (gpu->limit && gpu->live == gpu->limit) { return ART_FULL; }
    for (size_t i = 0; i < ART_CACHE_SLOTS; ++i) {
        FakeTexture *entry = &gpu->textures[i];
        if (!entry->id) {
            entry->id = ++gpu->serial; entry->bytes = width * height * 4;
            entry->invalid = false; *out = entry->id;
            gpu->bytes += entry->bytes; ++gpu->uploads; ++gpu->live;
            if (gpu->bytes > gpu->peak) { gpu->peak = gpu->bytes; }
            CHECK(gpu->bytes <= ART_GPU_BYTES); return ART_OK;
        }
    }
    return ART_FULL;
}

static void release(void *context, UiTexture texture)
{
    FakeGpu *gpu = context;
    CHECK(GetCurrentThreadId() == gpu->owner && texture != 0);
    for (size_t i = 0; i < ART_CACHE_SLOTS; ++i) {
        FakeTexture *entry = &gpu->textures[i];
        if (entry->id == texture) {
            gpu->bytes -= entry->bytes; --gpu->live; ++gpu->releases;
            memset(entry, 0, sizeof(*entry)); return;
        }
    }
    CHECK(false); /* Includes double release and stale texture destruction. */
}

static bool valid(void *context, UiTexture texture)
{
    FakeGpu *gpu = context;
    CHECK(GetCurrentThreadId() == gpu->owner);
    for (size_t i = 0; i < ART_CACHE_SLOTS; ++i) {
        if (gpu->textures[i].id == texture) { return !gpu->textures[i].invalid; }
    }
    return false;
}

static ArtworkGpu callbacks(FakeGpu *gpu)
{
    gpu->owner = GetCurrentThreadId();
    return (ArtworkGpu){upload, release, valid, gpu};
}

static ArtworkId identity(uint32_t number, uint64_t generation)
{
    ArtworkId id = {.size = 256, .generation = generation};
    for (size_t i = 0; i < sizeof(number); ++i) {
        id.content[i] = (uint8_t)(number >> (i * 8));
    }
    return id;
}

static unsigned char rgba[ART_UPLOAD_BYTES];
static UiRenderFrame frame;

static void cache_pressure(void)
{
    FakeGpu gpu = {0}; TextureCache cache;
    ArtworkHandle handles[ART_GPU_BYTES / ART_UPLOAD_BYTES]; UiTexture texture;
    texture_cache_init(&cache, callbacks(&gpu));
    for (uint32_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
        CHECK(texture_cache_insert(&cache, identity(i + 1, 1), 256, 256,
            rgba, &handles[i]) == ART_OK);
        CHECK(texture_cache_resolve(&cache, handles[i], &texture));
    }
    CHECK(cache.bytes == ART_GPU_BYTES && gpu.bytes == ART_GPU_BYTES);
    ArtworkHandle extra = {0};
    CHECK(texture_cache_insert(&cache, identity(99, 1), 256, 256, rgba, &extra) == ART_FULL);
    CHECK(gpu.releases == 0 && gpu.uploads == 16);
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
        CHECK(texture_cache_resolve(&cache, handles[i], &texture));
    }
    texture_cache_prepare(&cache); /* Prior frame is consumed; visible set ends. */
    CHECK(texture_cache_insert(&cache, identity(99, 1), 256, 256, rgba, &extra) == ART_FULL);
    CHECK(gpu.releases == 0); /* Deferred until the next safe prepare boundary. */
    texture_cache_prepare(&cache);
    CHECK(gpu.releases > 0 && !texture_cache_resolve(&cache, handles[0], &texture));
    CHECK(texture_cache_insert(&cache, identity(99, 1), 256, 256, rgba, &extra) == ART_OK);
    CHECK(texture_cache_resolve(&cache, extra, &texture));
    texture_cache_clear(&cache);
    CHECK(!texture_cache_resolve(&cache, extra, &texture));
    CHECK(cache.bytes == 0 && gpu.live == 0 && gpu.uploads == gpu.releases);
}

static void cache_churn(void)
{
    FakeGpu gpu = {0}; TextureCache cache; ArtworkHandle stale = {0};
    texture_cache_init(&cache, callbacks(&gpu));
    for (uint32_t i = 1; i <= 500; ++i) {
        ArtworkHandle handle; UiTexture texture;
        texture_cache_prepare(&cache);
        ArtResult result = texture_cache_insert(&cache, identity(i, 1), 256, 256, rgba, &handle);
        if (result == ART_FULL) {
            texture_cache_prepare(&cache);
            result = texture_cache_insert(&cache, identity(i, 1), 256, 256, rgba, &handle);
        }
        CHECK(result == ART_OK);
        CHECK(texture_cache_find(&cache, identity(i, 1), &handle));
        CHECK(texture_cache_resolve(&cache, handle, &texture));
        CHECK(cache.bytes == gpu.bytes && cache.bytes <= ART_GPU_BYTES);
        if (i == 1) { stale = handle; }
        if (i > 32) { CHECK(!texture_cache_resolve(&cache, stale, &texture)); }
    }
    CHECK(cache.peak_bytes == ART_GPU_BYTES && gpu.peak == ART_GPU_BYTES);
    texture_cache_clear(&cache);
    CHECK(gpu.live == 0 && gpu.bytes == 0 && gpu.uploads == 500 && gpu.releases == 500);
    ArtworkHandle again; UiTexture texture;
    CHECK(texture_cache_insert(&cache, identity(1, 1), 4, 2, rgba, &again) == ART_OK);
    CHECK(again.serial != stale.serial && !texture_cache_resolve(&cache, stale, &texture));
    gpu.textures[0].invalid = true; /* Device reset: backend handle no longer usable. */
    CHECK(!texture_cache_resolve(&cache, again, &texture));
    texture_cache_clear(&cache); CHECK(gpu.uploads == gpu.releases);
    CHECK(artwork_id_equal(identity(1, 1), identity(1, 1)));
    CHECK(!artwork_id_equal(identity(1, 1), identity(2, 1)));
    CHECK(!artwork_id_equal(identity(1, 1), identity(1, 2)));
    ArtworkId smaller = identity(1, 1); smaller.size = 128;
    CHECK(!artwork_id_equal(identity(1, 1), smaller));
}

static void cache_slots_and_failures(void)
{
    FakeGpu gpu = {0}; TextureCache cache; ArtworkHandle first = {0}, handle;
    UiTexture texture;
    texture_cache_init(&cache, callbacks(&gpu));
    gpu.fail = ART_FULL;
    CHECK(texture_cache_insert(&cache, identity(1, 1), 4, 2, rgba, &handle) == ART_FULL);
    CHECK(cache.bytes == 0 && gpu.bytes == 0 && gpu.uploads == 0);
    gpu.fail = ART_OK;
    for (uint32_t i = 1; i <= ART_CACHE_SLOTS; ++i) {
        CHECK(texture_cache_insert(&cache, identity(i, 1), 4, 2, rgba, &handle) == ART_OK);
        if (i == 1) { first = handle; }
    }
    CHECK(cache.bytes == ART_CACHE_SLOTS * 32 && cache.bytes < ART_GPU_BYTES);
    CHECK(texture_cache_insert(&cache, identity(99, 1), 4, 2, rgba, &handle) == ART_FULL);
    for (uint32_t i = 99; i < 140; ++i) {
        texture_cache_prepare(&cache);
        CHECK(texture_cache_resolve(&cache, first, &texture)); /* Visible oldest survives. */
        CHECK(texture_cache_insert(&cache, identity(i, 1), 4, 2, rgba, &handle) == ART_FULL);
        texture_cache_prepare(&cache);
        CHECK(texture_cache_resolve(&cache, first, &texture));
        CHECK(texture_cache_insert(&cache, identity(i, 1), 4, 2, rgba, &handle) == ART_OK);
    }
    texture_cache_clear(&cache);
    CHECK(gpu.bytes == 0 && gpu.live == 0 && gpu.uploads == gpu.releases);
}
static void shared_renderer_slots(void)
{
    FakeGpu gpu = {.limit=2}; TextureCache cache;
    ArtworkHandle first, second, third; UiTexture texture;
    texture_cache_init(&cache,callbacks(&gpu));
    CHECK(texture_cache_insert(&cache,identity(1,1),4,2,rgba,&first) == ART_OK);
    CHECK(texture_cache_insert(&cache,identity(2,1),4,2,rgba,&second) == ART_OK);
    texture_cache_prepare(&cache);
    CHECK(texture_cache_resolve(&cache,second,&texture));
    CHECK(texture_cache_insert(&cache,identity(3,1),4,2,rgba,&third) == ART_FULL);
    CHECK(cache.bytes == 64 && gpu.releases == 0 && !texture_cache_resolve(&cache,first,&texture));
    CHECK(texture_cache_resolve(&cache,second,&texture));
    texture_cache_prepare(&cache);
    CHECK(gpu.releases == 1 && texture_cache_resolve(&cache,second,&texture));
    CHECK(texture_cache_insert(&cache,identity(3,1),4,2,rgba,&third) == ART_OK);
    CHECK(gpu.live == 2 && cache.bytes == 64);
    texture_cache_clear(&cache); CHECK(gpu.uploads == gpu.releases);
}

typedef struct FetchFixture {
    HANDLE started, release;
    DWORD owner;
    bool gated, large;
    _Atomic uint32_t calls;
} FetchFixture;

static ArtResult fetch(void *context, const wchar_t *url, void *buffer,
    size_t capacity, size_t *bytes, const CancelToken *cancel)
{
    FetchFixture *fixture = context;
    CHECK(GetCurrentThreadId() != fixture->owner && cancel != NULL);
    CHECK(wcscmp(url, L"https://example.test/synthetic.png") == 0);
    CHECK(capacity <= ART_COMPRESSED_BYTES && capacity >= sizeof(art_fixture_png));
    atomic_fetch_add(&fixture->calls, 1);
    CHECK(SetEvent(fixture->started));
    if (fixture->gated) {
        /* A bounded slow transport deliberately delays cancellation retirement. */
        CHECK(WaitForSingleObject(fixture->release, 5000) == WAIT_OBJECT_0);
    }
    if (worker_cancelled(cancel)) { *bytes = 0; return ART_CANCELLED; }
    const unsigned char *data = fixture->large ? art_fixture_jpeg_max : art_fixture_png;
    *bytes = fixture->large ? sizeof(art_fixture_jpeg_max) : sizeof(art_fixture_png);
    CHECK(*bytes <= capacity); memcpy(buffer, data, *bytes); return ART_OK;
}

typedef struct Fixture {
    _Alignas(16) unsigned char core_memory[MEMORY_BUDGET_CORE];
    _Alignas(16) unsigned char storage[65536], decode[ART_DECODE_BYTES];
    unsigned char upload[ART_UPLOAD_BYTES];
    Arena core; Metrics metrics; MetricsArena core_binding;
    Worker *worker; Artwork *art; FakeGpu gpu; FetchFixture fetch;
} Fixture;
static Fixture fixture;

static void start(bool gated)
{
    Fixture *f = &fixture; memset(f, 0, sizeof(*f));
    CHECK(metrics_init(&f->metrics));
    CHECK(arena_init(&f->core, f->core_memory, sizeof(f->core_memory)));
    CHECK(metrics_attach_arena(&f->metrics, &f->core, &f->core_binding));
    CHECK(worker_start(&f->core, ART_REQUESTS, (CoreClock){0}, &f->worker) == CORE_OK);
    f->fetch.owner = GetCurrentThreadId(); f->fetch.gated = gated;
    f->fetch.started = CreateEventW(NULL, TRUE, FALSE, NULL);
    f->fetch.release = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(f->fetch.started != NULL && f->fetch.release != NULL);
    ArtworkConfig config = {.worker=f->worker, .fetch=fetch, .fetch_context=&f->fetch,
        .gpu=callbacks(&f->gpu), .decode_workspace=f->decode, .upload=f->upload,
        .metrics=&f->metrics};
    CHECK(artwork_size() <= sizeof(f->storage));
    CHECK(artwork_init(f->storage, sizeof(f->storage), &config, &f->art) == ART_OK);
    CHECK(artwork_wait_handle(f->art) != NULL);
    CHECK(artwork_navigate(f->art, 1) == ART_OK);
}

static void finish(void)
{
    Fixture *f = &fixture;
    ArtResult result = artwork_stop(f->art);
    for (unsigned retry = 0; result == ART_PENDING && retry < 4; ++retry) {
        CHECK(SetEvent(f->fetch.release));
        CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 5000) == WAIT_OBJECT_0);
        result = artwork_stop(f->art);
    }
    CHECK(result == ART_OK);
    CHECK(worker_stop(f->worker) == CORE_OK && worker_join(f->worker, 5000) == CORE_OK);
    CHECK(f->gpu.live == 0 && f->gpu.uploads == f->gpu.releases);
    CHECK(CloseHandle(f->fetch.started) && CloseHandle(f->fetch.release));
    arena_dispose(&f->core);
    CHECK(metrics_balanced(&f->metrics));
}

static ArtworkSource source(void)
{
    return (ArtworkSource){L"https://example.test/synthetic.png", 4, 2};
}

static void async_upload(void)
{
    start(false); Fixture *f = &fixture;
    wchar_t copied_url[] = L"https://example.test/synthetic.png";
    ArtworkSource image = {copied_url, 4, 2}; ArtworkId id = identity(1, 1);
    CHECK(artwork_request(f->art, id, &image, 1) == ART_PENDING);
    copied_url[0] = L'X'; /* Request must retain a bounded copy, not caller storage. */
    CHECK(atomic_load(&f->fetch.calls) == 0 && f->gpu.uploads == 0);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 5000) == WAIT_OBJECT_0);
    CHECK(f->gpu.uploads == 0); /* Completion cannot invoke the UI GPU backend. */
    CHECK(artwork_prepare(f->art) == ART_OK);
    ArtworkHandle handle; UiTexture texture;
    CHECK(artwork_lookup(f->art, id, &handle) && artwork_resolve(f->art, handle, &texture));
#if defined(_MSC_VER) && defined(_DEBUG)
    watch_heap = true;
#endif
    for (unsigned i = 0; i < 1000; ++i) {
        CHECK(artwork_lookup(f->art, id, &handle) && artwork_resolve(f->art, handle, &texture));
        CHECK(ui_frame_begin(&frame, 64, 64, 1) == UI_RENDER_OK);
        CHECK(artwork_draw(f->art, id, &frame, (UiRect){0, 0, 32, 32}) == UI_RENDER_OK);
        CHECK(artwork_draw(f->art, identity(999, 1), &frame, (UiRect){32, 0, 32, 32}) == UI_RENDER_OK);
        CHECK(frame.quads[0].effect[0] == UI_QUAD_IMAGE && frame.batches[0].texture == texture);
        CHECK(frame.quads[1].effect[0] == UI_QUAD_SOLID && frame.batches[1].texture == 0);
    }
#if defined(_MSC_VER) && defined(_DEBUG)
    watch_heap = false; CHECK(heap_calls == 0);
#endif
    ArtworkStats stats = artwork_stats(f->art);
    CHECK(stats.uploaded == 1 && stats.pending == 0 && !stats.active);
    CHECK(stats.gpu_bytes == 4 * 2 * 4 && stats.decode.live_objects == 0);
    CHECK(atomic_load(&f->fetch.calls) == 1 && f->gpu.uploads == 1);
    image = source();
    CHECK(artwork_request(f->art, id, &image, 1) == ART_OK);
    CHECK(artwork_prepare(f->art) == ART_OK && atomic_load(&f->fetch.calls) == 1);
    CHECK(artwork_navigate(f->art, 2) == ART_OK);
    CHECK(artwork_navigate(f->art, 1) == ART_STALE);
    CHECK(artwork_navigate(f->art, 2) == ART_OK);
    CHECK(!artwork_resolve(f->art, handle, &texture));
    CHECK(!artwork_lookup(f->art, id, &handle));
    finish();
}

static void invalid_sources(void)
{
    start(false); Fixture *f = &fixture; ArtworkId id = identity(1, 1);
    ArtworkSource image = {L"http://example.test/synthetic.png", 4, 2};
    CHECK(artwork_request(f->art, id, &image, 1) == ART_INVALID);
    image = source(); image.width = 0;
    CHECK(artwork_request(f->art, id, &image, 1) == ART_UNSUPPORTED);
    image.width = ART_ORIGINAL_EDGE + 1;
    CHECK(artwork_request(f->art, id, &image, 1) == ART_UNSUPPORTED);
    wchar_t long_url[ART_URL_CHARS + 1];
    for (size_t i = 0; i < ART_URL_CHARS; ++i) { long_url[i] = L'a'; }
    memcpy(long_url, L"https://", 8 * sizeof(wchar_t)); long_url[ART_URL_CHARS] = 0;
    image = (ArtworkSource){long_url, 4, 2};
    CHECK(artwork_request(f->art, id, &image, 1) == ART_INVALID);
    image = source();
    CHECK(artwork_request(f->art, id, &image, 33) == ART_INVALID);
    id.size = 0; CHECK(artwork_request(f->art, id, &image, 1) == ART_INVALID);
    id.size = 64;
    ArtworkSource variants[] = {
        {L"https://example.test/oversized.png", 512, 512},
        {L"https://example.test/too-small.png", 4, 2},
        {L"https://example.test/synthetic.png", 64, 64},
        {L"https://example.test/needlessly-large.png", 256, 256}
    };
    CHECK(artwork_request(f->art, id, variants, 4) == ART_PENDING);
    CHECK(artwork_request(f->art, id, variants, 4) == ART_PENDING);
    CHECK(artwork_stats(f->art).pending == 1);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 5000) == WAIT_OBJECT_0);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(atomic_load(&f->fetch.calls) == 1);
    finish();
}

static void stale_upload(void)
{
    start(false); Fixture *f = &fixture; ArtworkSource image = source();
    CHECK(artwork_request(f->art, identity(1, 1), &image, 1) == ART_PENDING);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 5000) == WAIT_OBJECT_0);
    CHECK(artwork_navigate(f->art, 2) == ART_OK);
    CHECK(artwork_prepare(f->art) == ART_STALE);
    CHECK(f->gpu.uploads == 0 && artwork_stats(f->art).stale == 1);
    finish();
}

static void admission_and_navigation(void)
{
    start(true); Fixture *f = &fixture; ArtworkSource image = source();
    for (uint32_t i = 1; i <= ART_REQUESTS; ++i) {
        CHECK(artwork_request(f->art, identity(i, 1), &image, 1) == ART_PENDING);
    }
    CHECK(artwork_request(f->art, identity(99, 1), &image, 1) == ART_FULL);
    CHECK(artwork_stats(f->art).pending == ART_REQUESTS);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(WaitForSingleObject(f->fetch.started, 5000) == WAIT_OBJECT_0);
    CHECK(artwork_navigate(f->art, 2) == ART_OK);
    CHECK(artwork_request(f->art, identity(99, 1), &image, 1) == ART_STALE);
    CHECK(SetEvent(f->fetch.release));
    CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 5000) == WAIT_OBJECT_0);
    CHECK(artwork_prepare(f->art) == ART_STALE && f->gpu.uploads == 0);
    ArtworkHandle handle;
    for (uint32_t i = 1; i <= ART_REQUESTS; ++i) {
        CHECK(!artwork_lookup(f->art, identity(i, 1), &handle));
    }
    CHECK(atomic_load(&f->fetch.calls) == 1);
    CHECK(artwork_request(f->art, identity(100, 2), &image, 1) == ART_PENDING);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 5000) == WAIT_OBJECT_0);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(artwork_lookup(f->art, identity(100, 2), &handle));
    CHECK(artwork_stats(f->art).uploaded == 1);
    finish();
}

static void shutdown_busy(void)
{
    start(true); Fixture *f = &fixture; ArtworkSource image = source();
    CHECK(artwork_request(f->art, identity(1, 1), &image, 1) == ART_PENDING);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(WaitForSingleObject(f->fetch.started, 5000) == WAIT_OBJECT_0);
    CHECK(artwork_stop(f->art) == ART_PENDING);
    CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 0) == WAIT_TIMEOUT);
    CHECK(f->gpu.uploads == 0 && worker_join(f->worker, 0) != CORE_OK);
    finish();
}

static void pipeline_pressure(void)
{
    start(false); Fixture *f = &fixture; f->fetch.large = true;
    ArtworkSource image = {L"https://example.test/synthetic.png", 256, 256};
    for (uint32_t i = 1; i <= 17; ++i) {
        CHECK(artwork_request(f->art, identity(i, 1), &image, 1) == ART_PENDING);
        CHECK(artwork_prepare(f->art) == ART_OK);
        CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 5000) == WAIT_OBJECT_0);
        CHECK(artwork_prepare(f->art) == (i <= 16 ? ART_OK : ART_FULL));
    }
    CHECK(f->gpu.bytes == ART_GPU_BYTES && f->gpu.uploads == 16 && f->gpu.releases == 0);
    ArtworkHandle handle;
    for (uint32_t i = 1; i <= 16; ++i) CHECK(artwork_lookup(f->art, identity(i, 1), &handle));
    CHECK(ui_frame_begin(&frame, 64, 64, 1) == UI_RENDER_OK);
    CHECK(artwork_draw(f->art, identity(17, 1), &frame, (UiRect){0, 0, 32, 32}) == UI_RENDER_OK);
    CHECK(frame.quads[0].effect[0] == UI_QUAD_SOLID);
    CHECK(WaitForSingleObject(artwork_wait_handle(f->art), 0) == WAIT_TIMEOUT); /* Idle under pin pressure. */
    CHECK(artwork_request(f->art, identity(17, 1), &image, 1) == ART_PENDING);
    CHECK(atomic_load(&f->fetch.calls) == 17);
    CHECK(artwork_begin_visible(f->art) == ART_OK);
    for (uint32_t i = 1; i <= 15; ++i) CHECK(artwork_lookup(f->art, identity(i, 1), &handle));
    CHECK(artwork_request(f->art, identity(17, 1), &image, 1) == ART_PENDING);
    CHECK(artwork_prepare(f->art) == ART_FULL); /* Retire only the now-hidden sixteenth entry. */
    CHECK(f->gpu.releases == 0);
    CHECK(artwork_prepare(f->art) == ART_OK);
    CHECK(f->gpu.releases == 1 && f->gpu.uploads == 17 && atomic_load(&f->fetch.calls) == 17);
    for (uint32_t i = 1; i <= 15; ++i) CHECK(artwork_lookup(f->art, identity(i, 1), &handle));
    CHECK(!artwork_lookup(f->art, identity(16, 1), &handle));
    CHECK(artwork_lookup(f->art, identity(17, 1), &handle));
    finish();
}

static void http_preflight(void)
{
    Metrics metrics; CHECK(metrics_init(&metrics));
    CancelToken cancel = {.requested = true, .deadline = CORE_CLOCK_NEVER};
    unsigned char buffer[16]; size_t bytes = 99;
    CHECK(artwork_http_fetch(&metrics, L"https://example.test/a.png", buffer,
        sizeof(buffer), &bytes, NULL) == ART_INVALID && bytes == 0);
    static const wchar_t *const invalid[] = {
        L"http://example.test/a.png", L"https://user:password@example.test/a.png",
        L"https://example.test/a.png#fragment", L"https://", L"file:///a.png"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        bytes = 99;
        CHECK(artwork_http_fetch(&metrics, invalid[i], buffer, sizeof(buffer),
            &bytes, &cancel) == ART_INVALID && bytes == 0);
    }
    CHECK(artwork_http_fetch(&metrics, L"https://example.test/a.png", buffer,
        ART_COMPRESSED_BYTES + 1, &bytes, &cancel) == ART_INVALID && bytes == 0);
    CHECK(artwork_http_fetch(&metrics, L"https://example.test/a.png", buffer,
        sizeof(buffer), &bytes, &cancel) == ART_CANCELLED && bytes == 0);
    CHECK(artwork_http_fetch(&metrics, NULL, buffer,
        sizeof(buffer), &bytes, &cancel) == ART_INVALID && bytes == 0);
    MetricsSnapshot stats = metrics_snapshot(&metrics);
    CHECK(stats.handles == 0 && stats.handle_high_water == 0 && stats.owned_allocations == 0);
    CHECK(metrics_balanced(&metrics));
}

void test_artwork_decode(void);
void test_artwork_visibility(void);
int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(allocation_hook);
#endif
    test_artwork_decode(); cache_pressure(); cache_churn(); cache_slots_and_failures(); shared_renderer_slots(); http_preflight();
    async_upload(); invalid_sources(); stale_upload(); admission_and_navigation(); shutdown_busy(); pipeline_pressure();
    test_artwork_visibility();
#if defined(_MSC_VER) && defined(_DEBUG)
    CHECK(heap_calls == 0); _CrtSetAllocHook(previous);
#endif
    puts("artwork: bounded decode/cache/worker ownership checks passed"); return 0;
}
