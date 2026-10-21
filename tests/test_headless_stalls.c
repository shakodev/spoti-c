#include "headless_support.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

enum { CHECK_FRAMES = 48000 * 10 };
typedef struct Recording { float samples[CHECK_FRAMES * 2]; size_t used; _Atomic bool ready; } Recording;
static Recording recording;
static void observe(void *context, const float *samples, size_t frames)
{
    Recording *r = context;
    if (atomic_load_explicit(&r->ready, memory_order_relaxed)) { return; }
    size_t take = CHECK_FRAMES - r->used; if (take > frames) { take = frames; }
    memcpy(r->samples + r->used * 2, samples, take * 2 * sizeof(float)); r->used += take;
    if (r->used == CHECK_FRAMES) { atomic_store_explicit(&r->ready, true, memory_order_release); }
}
static void repeated_seek(HeadlessFixture *f)
{
    atomic_store(&f->server.delay_ms, 0);
    HeadlessConfig c = {.cache_directory=f->cache, .fixture_url=f->url};
    Headless *h; CHECK(headless_open(&c, &h)); hf_load(h, false); hf_wait(h, PLAYER_PAUSED);
    /* Exceed the range request table several times in one lifetime. Every seek
     * retires a source with a reserved or in-flight look-ahead request. */
    for (unsigned i = 0; i < 40; ++i) {
        uint64_t generation = headless_snapshot(h).generation;
        CHECK(headless_command(h, i % 2 ? "seek 1500" : "seek 0"));
        uint64_t until = GetTickCount64() + 5000;
        for (;;) {
            PlayerSnapshot view = headless_snapshot(h);
            if (view.generation != generation && view.state == PLAYER_PAUSED) { break; }
            CHECK(view.state != PLAYER_ERROR && GetTickCount64() < until); Sleep(1);
        }
    }
    HeadlessMetrics metrics; CHECK(headless_close(h, &metrics) && metrics.balanced);
}
int main(void)
{
    HeadlessFixture f; hf_open(&f);
    atomic_store(&f.server.delay_ms, 150);
    HeadlessConfig c = {.cache_directory=f.cache, .fixture_url=f.url, .realtime=true,
        .diagnostics=true, .observe=observe, .observe_context=&recording};
    Headless *h; CHECK(headless_open(&c, &h)); hf_load(h, true);
    uint64_t until = GetTickCount64() + 20000;
    while (!atomic_load_explicit(&recording.ready, memory_order_acquire) && GetTickCount64() < until) {
        CHECK(headless_snapshot(h).state != PLAYER_ERROR); Sleep(1);
    }
    CHECK(atomic_load_explicit(&recording.ready, memory_order_acquire));
    CHECK(headless_command(h, "pause")); hf_wait(h, PLAYER_PAUSED);
    HeadlessMetrics metrics; CHECK(headless_close(h, &metrics) && metrics.balanced);
    printf("Cold-cache 150 ms responses: %llu underrun frames in ten seconds.\n",
        (unsigned long long)metrics.diagnostics.values[AD_UNDERRUN_FRAMES].total);
    CHECK(metrics.diagnostics.values[AD_RANGE_TICKS].maximum >= metrics.qpc_frequency * 15 / 100);
    CHECK(metrics.diagnostics.values[AD_UNDERRUN_FRAMES].total == 0);
    FILE *file = NULL; CHECK(fopen_s(&file, HEADLESS_FIXTURE_DIR "/synthetic.f32", "rb") == 0);
    float golden[960]; size_t compared = 0;
    while (compared < CHECK_FRAMES) {
        size_t n = CHECK_FRAMES - compared; if (n > 480) { n = 480; }
        CHECK(fread(golden, sizeof(float) * 2, n, file) == n);
        for (size_t i = 0; i < n * 2; ++i) { CHECK(fabsf(golden[i] - recording.samples[compared * 2 + i]) < .00003f); }
        compared += n;
    }
    CHECK(fclose(file) == 0);
    repeated_seek(&f); hf_close(&f);
    puts("Continuous device periods preserve every source sample across delayed HTTP block boundaries."); return 0;
}
