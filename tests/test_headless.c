#include "headless_support.h"
#include <math.h>
#include <string.h>
static FILE *golden;
static uint64_t compared;
static atomic_uint state_edges;
void headless_volume_receipts(void);
static void state_changed(void *context) { atomic_fetch_add((atomic_uint *)context,1); }
static void compare(void *ctx, const float *pcm, size_t frames)
{
    (void)ctx; float expected[960]; CHECK(frames <= 480);
    if (compared == 576137) { CHECK(_fseeki64(golden, 0, SEEK_SET) == 0); compared = 0; }
    size_t first = (size_t)(576137 - compared); if (first > frames) { first = frames; }
    CHECK(fread(expected, 2 * sizeof(float), first, golden) == first);
    if (first < frames) {
        CHECK(_fseeki64(golden, 0, SEEK_SET) == 0);
        CHECK(fread(expected + first * 2, 2 * sizeof(float), frames - first, golden) == frames - first);
    }
    for (size_t i = 0; i < frames * 2; ++i) { CHECK(isfinite(pcm[i]) && fabsf(pcm[i] - expected[i]) < 0.00003f); }
    compared = first < frames ? frames - first : compared + frames;
}
static void parser(void)
{
    PlayerCommand c = {0};
    CHECK(!headless_parse(NULL,48000,&c) && !headless_parse("play",48000,NULL));
    CHECK(!headless_parse("seek 1",0,&c));
    const char *controls[] = {"play","pause","next","stop"};
    const PlayerCommandType types[] = {PLAYER_PLAY,PLAYER_PAUSE,PLAYER_NEXT,PLAYER_STOP};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(headless_parse(controls[i],48000,&c) && c.type == types[i]);
    }
    CHECK(headless_parse("seek 1234", 48000, &c) && c.position_frames == 59232);
    CHECK(headless_parse("volume 0.5", 48000, &c) && c.volume == 0.5f);
    const char *invalid[] = {"seek -1","seek ","seek 18446744073709551615","seek 1x","volume nan",
        "volume inf","volume 2","volume -1","volume 0.5 garbage","play garbage","pause ",""};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) { CHECK(!headless_parse(invalid[i],48000,&c)); }
}
static void failed_startup(void)
{
    HeadlessMetrics m = {.balanced = true};
    CHECK(!headless_close(NULL,&m) && !m.balanced);
    CHECK(!headless_process_metrics(NULL));
    Headless *h = (Headless *)(uintptr_t)1;
    CHECK(!headless_open(NULL,&h) && h == NULL);
    HeadlessConfig config = {.cache_directory = L".", .fixture_url = "invalid-fixture-origin"};
    CHECK(!headless_open(&config,&h) && h == NULL); /* Warm thread/runtime bookkeeping. */
    DWORD before, after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    for (unsigned i = 0; i < 16; ++i) { CHECK(!headless_open(&config,&h) && h == NULL); }
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && after == before);
}
static void duration_and_gapless(HeadlessFixture *f)
{
    for (unsigned run = 0; run < 2; ++run) {
        CHECK(fopen_s(&golden, HEADLESS_FIXTURE_DIR "/synthetic.f32", "rb") == 0); compared = 0;
        atomic_init(&state_edges,0);
        Headless *h; HeadlessConfig c = {.cache_directory = f->cache, .fixture_url = f->url, .observe = compare,
            .state_changed=state_changed,.state_context=&state_edges};
        CHECK(headless_open(&c, &h)); hf_load(h, false);
        uint64_t clock=headless_snapshot(h).clock_serial;
        CHECK(headless_refresh_clock(h)==PLAYER_OK);
        uint64_t until=GetTickCount64()+2000;
        while (headless_snapshot(h).clock_serial==clock && GetTickCount64()<until) { Sleep(1); }
        CHECK(headless_snapshot(h).clock_serial==clock+1);
        PlayerCommand next = {.type = PLAYER_QUEUE_APPEND}; next.track_id[15] = 2;
        CHECK(headless_submit(h, &next) == PLAYER_OK); CHECK(headless_command(h, "play"));
        hf_wait(h, PLAYER_ENDED); HeadlessMetrics m;
        CHECK(headless_metrics(h, &m)); CHECK(m.frames == 2 * UINT64_C(576137) && m.underruns == 0);
        uint64_t peak = m.peak_working_set;
        bool closed = headless_close(h, &m);
        if (!closed || !m.balanced) { headless_write_metrics(stderr, &m); }
        CHECK(closed && m.balanced && m.network_attempts + m.cache_hits > 0);
        CHECK(atomic_load(&state_edges)>=3 && atomic_load(&state_edges)<m.callbacks);
        CHECK(m.peak_working_set >= peak && m.peak_working_set >= m.working_set);
        CHECK(m.working_set > 0 && m.process_handles > 0);
        if (run) { CHECK(m.cache_hits > 0); }
        CHECK(fclose(golden) == 0);
    }
}
static void controls_and_cancel(HeadlessFixture *f)
{
    for (unsigned run = 0; run < 12; ++run) {
        Headless *h; HeadlessConfig c = {.cache_directory = f->cache, .fixture_url = f->url, .realtime = true};
        CHECK(headless_open(&c, &h)); hf_load(h, true);
        if (run % 3 == 0) {
            hf_wait(h, PLAYER_PLAYING); CHECK(headless_command(h,"pause")); hf_wait(h,PLAYER_PAUSED);
            CHECK(headless_command(h,"seek 1500")); CHECK(headless_command(h,"volume 0.25"));
            CHECK(headless_command(h,"play")); hf_wait(h, PLAYER_PLAYING);
            CHECK(headless_command(h,"next")); hf_wait(h, PLAYER_ENDED);
            CHECK(headless_command(h,"stop")); hf_wait(h,PLAYER_STOPPED);
        }
        HeadlessMetrics m; CHECK(headless_close(h,&m) && m.balanced);
    }
}
static void source_errors(void)
{
    for (unsigned fault = 1; fault <= 3; ++fault) {
        HeadlessFixture f; hf_open(&f); atomic_store(&f.server.fault, fault);
        HeadlessConfig config = {.cache_directory = f.cache, .fixture_url = f.url}; Headless *h;
        CHECK(headless_open(&config,&h)); hf_load(h,true); hf_wait(h,PLAYER_ERROR);
        HeadlessMetrics m; CHECK(headless_close(h,&m) && m.balanced); hf_close(&f);
    }
}
static void device_import_startup(void)
{
    /* Reject before opening any physical device. Device import initialization
     * belongs to the caller, so later decode owners must still be non-GUI. */
    HeadlessConfig bad = {.cache_directory = L".", .fixture_url = "invalid", .wasapi = true};
    Headless *h; CHECK(!headless_open(&bad,&h) && h == NULL);
    HeadlessFixture f; hf_open(&f);
    HeadlessConfig good = {.cache_directory = f.cache, .fixture_url = f.url};
    CHECK(headless_open(&good,&h)); hf_load(h,true); hf_wait(h,PLAYER_ENDED);
    HeadlessMetrics m; CHECK(headless_close(h,&m) && m.balanced); hf_close(&f);
}
static void idle_listener(void)
{
    HeadlessFixture f; hf_open(&f);
    Sleep(11000); /* Regression: listener previously exited after 10 idle seconds. */
    Headless *h; HeadlessConfig config={.cache_directory=f.cache,.fixture_url=f.url};
    CHECK(headless_open(&config,&h)); hf_load(h,true); hf_wait(h,PLAYER_ENDED);
    HeadlessMetrics m; CHECK(headless_close(h,&m)&&m.balanced&&m.frames==576137); hf_close(&f);
}
int main(void)
{
    headless_volume_receipts();
    parser(); failed_startup(); idle_listener(); HeadlessFixture f; hf_open(&f);
    duration_and_gapless(&f); controls_and_cancel(&f); hf_close(&f); source_errors(); device_import_startup();
    puts("Encrypted HTTP/cache/CTR/header/Vorbis/gapless duration, commands and cancellation passed."); return 0;
}
