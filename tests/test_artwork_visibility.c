#include "ui/artwork.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include "support/ui_heap_watch.h"
#include "artwork_fixtures.h"
#include <math.h>
#include <string.h>
#include <wchar.h>

typedef struct VisibilityFixture {
    _Alignas(16) unsigned char core_memory[MEMORY_BUDGET_CORE];
    _Alignas(16) unsigned char storage[65536], decode[ART_DECODE_BYTES];
    unsigned char upload[ART_UPLOAD_BYTES];
    Arena core;
    Worker *worker;
    Artwork *art;
    HANDLE started, release, notified, notify_release;
    DWORD owner;
    UiTexture textures[ART_CACHE_SLOTS], serial;
    uint32_t uploads, releases, live;
    _Atomic uint32_t fetches, wakes;
    ArtResult fetch_result, upload_result;
    bool gated, broken, notifier_gated, invalid_texture;
} VisibilityFixture;
static VisibilityFixture v;
static UiRenderFrame frame;

static ArtworkId id(uint32_t n, uint64_t generation)
{
    ArtworkId out = {.size=256,.generation=generation};
    memcpy(out.content,&n,sizeof(n)); return out;
}
static ArtworkSource source(void)
{
    return (ArtworkSource){L"https://example.test/synthetic.png",4,2};
}
static ArtResult fetch(void *context, const wchar_t *url, void *buffer,
    size_t capacity, size_t *bytes, const CancelToken *cancel)
{
    VisibilityFixture *f = context;
    CHECK(GetCurrentThreadId() != f->owner && capacity >= sizeof(art_fixture_png));
    CHECK(wcscmp(url,L"https://example.test/synthetic.png") == 0);
    atomic_fetch_add(&f->fetches,1);
    CHECK(SetEvent(f->started));
    if (f->gated) CHECK(WaitForSingleObject(f->release,5000) == WAIT_OBJECT_0);
    if (worker_cancelled(cancel)) { *bytes = 0; return ART_CANCELLED; }
    if (f->fetch_result != ART_OK) { *bytes = 0; return f->fetch_result; }
    memcpy(buffer,art_fixture_png,sizeof(art_fixture_png));
    if (f->broken) ((unsigned char *)buffer)[sizeof(art_fixture_png)-1] ^= 1;
    *bytes = sizeof(art_fixture_png); return ART_OK;
}
static void notify(void *context)
{
    VisibilityFixture *f = context;
    CHECK(!f->notifier_gated || GetCurrentThreadId() != f->owner);
    atomic_fetch_add(&f->wakes,1);
    CHECK(SetEvent(f->notified));
    if (f->notifier_gated) CHECK(WaitForSingleObject(f->notify_release,5000) == WAIT_OBJECT_0);
}
static ArtResult upload(void *context, uint32_t width, uint32_t height,
    const unsigned char *rgba, UiTexture *texture)
{
    VisibilityFixture *f = context;
    CHECK(GetCurrentThreadId() == f->owner && width == 4 && height == 2 && rgba);
    if (f->upload_result != ART_OK) return f->upload_result;
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) {
        if (!f->textures[i]) {
            *texture = f->textures[i] = ++f->serial; ++f->uploads; ++f->live; return ART_OK;
        }
    }
    return ART_FULL;
}
static void release(void *context, UiTexture texture)
{
    VisibilityFixture *f = context;
    CHECK(GetCurrentThreadId() == f->owner);
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) {
        if (f->textures[i] == texture) {
            f->textures[i] = 0; ++f->releases; --f->live; return;
        }
    }
    CHECK(false);
}
static bool valid(void *context, UiTexture texture)
{
    VisibilityFixture *f = context;
    CHECK(GetCurrentThreadId() == f->owner);
    if (f->invalid_texture) return false;
    for (unsigned i = 0; i < ART_CACHE_SLOTS; ++i) if (f->textures[i] == texture) return true;
    return false;
}
static void start(bool gated)
{
    memset(&v,0,sizeof(v)); v.owner = GetCurrentThreadId(); v.gated = gated;
    CHECK(arena_init(&v.core,v.core_memory,sizeof(v.core_memory)));
    CHECK(worker_start(&v.core,ART_REQUESTS,(CoreClock){0},&v.worker) == CORE_OK);
    v.started = CreateEventW(NULL,TRUE,FALSE,NULL); v.release = CreateEventW(NULL,TRUE,FALSE,NULL);
    v.notified = CreateEventW(NULL,TRUE,FALSE,NULL); v.notify_release = CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(v.started && v.release && v.notified && v.notify_release);
    ArtworkConfig config = {.worker=v.worker,.fetch=fetch,.fetch_context=&v,
        .gpu={upload,release,valid,&v},.decode_workspace=v.decode,.upload=v.upload,
        .wake=notify,.wake_context=&v};
    CHECK(artwork_size() <= sizeof(v.storage));
    CHECK(artwork_init(v.storage,sizeof(v.storage),&config,&v.art) == ART_OK);
    CHECK(artwork_navigate(v.art,1) == ART_OK);
}
static void finish(bool stopped)
{
    CHECK(SetEvent(v.release) && SetEvent(v.notify_release));
    ArtResult result = stopped ? ART_OK : artwork_stop(v.art);
    for (unsigned i = 0; result == ART_PENDING && i < 4; ++i) {
        CHECK(WaitForSingleObject(artwork_wait_handle(v.art),5000) == WAIT_OBJECT_0);
        result = artwork_stop(v.art);
    }
    CHECK(result == ART_OK);
    CHECK(worker_stop(v.worker) == CORE_OK && worker_join(v.worker,5000) == CORE_OK);
    CHECK(!v.live && v.uploads == v.releases);
    CHECK(CloseHandle(v.started) && CloseHandle(v.release));
    CHECK(CloseHandle(v.notified) && CloseHandle(v.notify_release));
    arena_dispose(&v.core);
}
static void wait_done(void)
{
    CHECK(WaitForSingleObject(artwork_wait_handle(v.art),5000) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(v.notified,5000) == WAIT_OBJECT_0);
}
static void load(ArtworkId image, ArtworkSource src, ArtResult result)
{
    CHECK(ResetEvent(v.notified));
    CHECK(artwork_request(v.art,image,&src,1) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK);
    wait_done();
    CHECK(artwork_prepare(v.art) == result);
}
static void offscreen_cancel(void)
{
    start(true); ArtworkSource src = source(); ArtworkHandle handle;
    CHECK(artwork_begin_visible(v.art) == ART_OK);
    for (uint32_t i = 1; i <= 3; ++i) CHECK(artwork_request(v.art,id(i,1),&src,1) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK);
    CHECK(WaitForSingleObject(v.started,5000) == WAIT_OBJECT_0);
    CHECK(artwork_begin_visible(v.art) == ART_OK);
    CHECK(!artwork_lookup(v.art,id(2,1),&handle)); /* A cache miss still retains the queued image. */
    CHECK(artwork_prepare(v.art) == ART_OK);
    CHECK(artwork_stats(v.art).pending == 1 && atomic_load(&v.fetches) == 1);
    CHECK(SetEvent(v.release)); wait_done();
    CHECK(artwork_prepare(v.art) == ART_CANCELLED); /* Starts retained second image. */
    wait_done(); CHECK(artwork_prepare(v.art) == ART_OK);
    CHECK(atomic_load(&v.fetches) == 2 && v.uploads == 1);
    CHECK(!artwork_lookup(v.art,id(1,1),&handle) && !artwork_lookup(v.art,id(3,1),&handle));
    CHECK(artwork_lookup(v.art,id(2,1),&handle));
    CHECK(artwork_error(v.art,id(1,1)) == ART_OK && artwork_stats(v.art).cancelled == 2);
    finish(false);

    start(false);
    CHECK(artwork_request(v.art,id(1,1),&src,1) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK); wait_done();
    CHECK(artwork_begin_visible(v.art) == ART_OK);
    CHECK(artwork_prepare(v.art) == ART_CANCELLED); /* Completion committed before cancellation. */
    CHECK(v.uploads == 0 && !artwork_stats(v.art).active);
    CHECK(artwork_error(v.art,id(1,1)) == ART_OK);
    finish(false);
}
static void reappearance(void)
{
    start(true); ArtworkSource src = source(); ArtworkHandle handle;
    CHECK(artwork_request(v.art,id(1,1),&src,1) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK);
    CHECK(WaitForSingleObject(v.started,5000) == WAIT_OBJECT_0);
    CHECK(artwork_begin_visible(v.art) == ART_OK && artwork_prepare(v.art) == ART_OK);
    CHECK(artwork_begin_visible(v.art) == ART_OK);
    CHECK(!artwork_lookup(v.art,id(1,1),&handle));
    CHECK(artwork_prepare(v.art) == ART_OK);
    CHECK(SetEvent(v.release)); wait_done();
    CHECK(artwork_prepare(v.art) == ART_CANCELLED); /* Exactly one replacement is admitted. */
    wait_done(); CHECK(artwork_prepare(v.art) == ART_OK);
    CHECK(artwork_lookup(v.art,id(1,1),&handle));
    CHECK(atomic_load(&v.fetches) == 2 && v.uploads == 1 && artwork_stats(v.art).pending == 0);
    CHECK(artwork_prepare(v.art) == ART_OK && atomic_load(&v.fetches) == 2);
    finish(false);
}
static void remembered_failures(void)
{
    start(false); v.broken = true; ArtworkSource src = source();
    load(id(1,1),src,ART_DECODE_ERROR);
    CHECK(artwork_error(v.art,id(1,1)) == ART_DECODE_ERROR);
    for (unsigned i = 0; i < 100; ++i) {
        CHECK(artwork_begin_visible(v.art) == ART_OK);
        CHECK(artwork_request(v.art,id(1,1),&src,1) == ART_DECODE_ERROR);
        CHECK(artwork_prepare(v.art) == ART_OK);
    }
    CHECK(atomic_load(&v.fetches) == 1 && v.uploads == 0);
    CHECK(WaitForSingleObject(artwork_wait_handle(v.art),0) == WAIT_TIMEOUT);
    CHECK(artwork_retry(v.art,id(1,1)) == ART_OK && atomic_load(&v.fetches) == 1);
    CHECK(artwork_error(v.art,id(1,1)) == ART_OK);
    v.broken = false; load(id(1,1),src,ART_OK);
    CHECK(v.uploads == 1 && atomic_load(&v.fetches) == 2);
    v.fetch_result = ART_IO_ERROR; load(id(2,1),src,ART_IO_ERROR);
    CHECK(artwork_request(v.art,id(2,1),&src,1) == ART_IO_ERROR);
    CHECK(artwork_prepare(v.art) == ART_OK && atomic_load(&v.fetches) == 3);
    v.fetch_result = ART_OK; v.upload_result = ART_IO_ERROR;
    load(id(3,1),src,ART_IO_ERROR);
    CHECK(artwork_error(v.art,id(3,1)) == ART_IO_ERROR);
    CHECK(artwork_request(v.art,id(3,1),&src,1) == ART_IO_ERROR);
    CHECK(artwork_prepare(v.art) == ART_OK && atomic_load(&v.fetches) == 4);
    finish(false);

    start(false); v.broken = true;
    for (uint32_t i = 1; i < ART_FAILURES; ++i) load(id(i,1),src,ART_DECODE_ERROR);
    CHECK(artwork_request(v.art,id(ART_FAILURES,1),&src,1) == ART_PENDING);
    CHECK(artwork_request(v.art,id(999,1),&src,1) == ART_FULL); /* Reserve the in-flight failure. */
    CHECK(artwork_prepare(v.art) == ART_OK); wait_done();
    CHECK(artwork_prepare(v.art) == ART_DECODE_ERROR);
    CHECK(artwork_request(v.art,id(999,1),&src,1) == ART_FULL);
    CHECK(artwork_prepare(v.art) == ART_OK && atomic_load(&v.fetches) == ART_FAILURES);
    CHECK(artwork_error(v.art,id(1,1)) == ART_DECODE_ERROR);
    CHECK(artwork_retry(v.art,id(1,1)) == ART_OK);
    load(id(999,1),src,ART_DECODE_ERROR);
    CHECK(artwork_error(v.art,id(2,1)) == ART_DECODE_ERROR);
    CHECK(artwork_navigate(v.art,2) == ART_OK);
    CHECK(artwork_error(v.art,id(2,1)) == ART_STALE && artwork_retry(v.art,id(2,1)) == ART_STALE);
    CHECK(artwork_error(v.art,id(2,2)) == ART_OK);
    v.broken = false; load(id(2,2),src,ART_OK);
    CHECK(atomic_load(&v.fetches) == ART_FAILURES+2);
    finish(false);
}
static void crop_and_stale(void)
{
    start(false); load(id(1,1),source(),ART_OK);
    ArtworkHandle handle; UiTexture texture = 0;
    CHECK(artwork_lookup(v.art,id(1,1),&handle));
    ui_heap_begin();
    for (unsigned i = 0; i < 1000; ++i) {
        CHECK(ui_frame_begin(&frame,200,200,1.25f) == UI_RENDER_OK);
        CHECK(artwork_draw_rounded(v.art,id(1,1),&frame,(UiRect){0,0,32,32},4) == UI_RENDER_OK);
        CHECK(frame.quads[0].uv[0] == 0.25f && frame.quads[0].uv[2] == 0.75f);
        CHECK(frame.quads[0].uv[1] == 0 && frame.quads[0].uv[3] == 1 && frame.quads[0].radii[0] == 4);
        CHECK(artwork_draw_rounded(v.art,id(2,1),&frame,(UiRect){32,0,32,32},8) == UI_RENDER_OK);
        CHECK(frame.quads[1].effect[0] == UI_QUAD_SOLID && frame.quads[1].radii[0] == 8);
    }
    ui_heap_end();
    v.invalid_texture = true;
    CHECK(!artwork_resolve(v.art,handle,&texture) && texture == 0);
    CHECK(ui_frame_begin(&frame,100,100,1) == UI_RENDER_OK);
    CHECK(artwork_draw_rounded(v.art,id(1,1),&frame,(UiRect){0,0,32,32},4) == UI_RENDER_OK);
    CHECK(frame.quads[0].effect[0] == UI_QUAD_SOLID);
    CHECK(artwork_begin_visible(v.art) == ART_OK && v.releases == 1);
    CHECK(artwork_draw_rounded(v.art,id(1,1),&frame,(UiRect){0,0,32,32},NAN) == UI_RENDER_INVALID);
    CHECK(ui_frame_begin(&frame,100,100,1) == UI_RENDER_OK);
    CHECK(artwork_draw_rounded(v.art,id(1,1),&frame,(UiRect){0,0,32,32},17) == UI_RENDER_INVALID);
    finish(false);
}
static void source_selection(void)
{
    start(false); ArtworkSource src = source(); src.width = src.height = 0;
    load(id(1,1),src,ART_OK); /* Unknown actual size is checked by decode. */
    src.width = 512; src.height = 256;
    load(id(2,1),src,ART_OK);
    ArtworkSource choices[] = {
        {L"https://example.test/unknown.png",0,0},
        {L"https://example.test/too-large.png",513,513},
        {L"https://example.test/large.png",512,512},
        {L"https://example.test/synthetic.png",256,256}
    };
    CHECK(ResetEvent(v.notified));
    CHECK(artwork_request(v.art,id(3,1),choices,4) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK); wait_done();
    CHECK(artwork_prepare(v.art) == ART_OK && v.uploads == 3);
    choices[0] = source(); choices[0].width = choices[0].height = 0;
    choices[2].width = choices[3].width = 0; /* Unpaired dimensions are skipped. */
    CHECK(artwork_request(v.art,id(4,1),choices,4) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK); wait_done();
    CHECK(artwork_prepare(v.art) == ART_OK && v.uploads == 4);
    finish(false);
}
static void notifier_retirement(void)
{
    start(false); v.notifier_gated = true; ArtworkSource src = source();
    CHECK(artwork_request(v.art,id(1,1),&src,1) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK); wait_done();
    CHECK(atomic_load(&v.wakes) == 1);
    CHECK(artwork_prepare(v.art) == ART_OK && v.uploads == 1);
    CHECK(artwork_stop(v.art) == ART_PENDING);
    CHECK(WaitForSingleObject(artwork_wait_handle(v.art),0) == WAIT_TIMEOUT);
    CHECK(SetEvent(v.notify_release));
    CHECK(WaitForSingleObject(artwork_wait_handle(v.art),5000) == WAIT_OBJECT_0);
    CHECK(artwork_stop(v.art) == ART_OK);
    CHECK(atomic_load(&v.wakes) == 1 && atomic_load(&v.fetches) == 1);
    finish(true);
}
static void retirement_wake(void)
{
    start(false); ArtworkSource src = source();
    load(id(1,1),src,ART_OK);
    CHECK(artwork_begin_visible(v.art) == ART_OK && ResetEvent(v.notified));
    CHECK(artwork_request(v.art,id(2,1),&src,1) == ART_PENDING);
    CHECK(artwork_prepare(v.art) == ART_OK); wait_done();
    v.upload_result = ART_FULL;
    CHECK(artwork_prepare(v.art) == ART_FULL);
    CHECK(v.releases == 0 && atomic_load(&v.wakes) == 3); /* Two completions and one new retirement. */
    v.upload_result = ART_OK;
    CHECK(artwork_prepare(v.art) == ART_OK && v.releases == 1 && v.uploads == 2);
    CHECK(atomic_load(&v.wakes) == 3 && WaitForSingleObject(artwork_wait_handle(v.art),0) == WAIT_TIMEOUT);
    v.upload_result = ART_FULL;
    load(id(3,1),src,ART_FULL); /* All cached entries remain pinned. */
    CHECK(atomic_load(&v.wakes) == 4);
    for (unsigned i = 0; i < 10; ++i) CHECK(artwork_prepare(v.art) == ART_FULL);
    CHECK(atomic_load(&v.wakes) == 4 && WaitForSingleObject(artwork_wait_handle(v.art),0) == WAIT_TIMEOUT);
    CHECK(artwork_begin_visible(v.art) == ART_OK && artwork_prepare(v.art) == ART_OK);
    CHECK(!artwork_stats(v.art).active);
    finish(false);
}
void test_artwork_visibility(void)
{
    offscreen_cancel(); reappearance(); remembered_failures();
    crop_and_stale(); source_selection(); notifier_retirement(); retirement_wake();
    puts("artwork visibility: cancellation, retry retention, crop, source admission and notifier retirement passed");
}
