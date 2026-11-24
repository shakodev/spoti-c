#include "player_test.h"

static void start_and_arm(void)
{
    /* A backend may synchronously request a burst during start. The production
     * headless ring is 8192 frames. Consume down to low water before arm, then
     * drive only actual producer wakes: no unconditional per-callback pump. */
    pt_open_bytes(64 * MEMORY_KIB); pt.start_frames = 6144;
    pt_track(PLAYER_LOAD, 2, true); pt_drive();
    CHECK(pt.starts == 1 && atomic_load(&pt.sink.underrun_frames) == 0);
    pt_compare(pt.output, pt.files[2].golden, pt.start_frames * 2);
    CHECK(spsc_readable(&pt.sink.ring) > pt.sink.low_water);
    size_t at = pt.start_frames;
    while (at < pt.files[2].frames) {
        size_t n = pt.files[2].frames - at; if (n > 480) { n = 480; }
        pt_render((uint32_t)n);
        pt_compare(pt.output, pt.files[2].golden + at * 2, n * 2); at += n;
        if (WaitForSingleObject(pt.sink.producer_event, 0) == WAIT_OBJECT_0) { pt_drive(); }
    }
    pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_ENDED);
    CHECK(atomic_load(&pt.sink.underrun_frames) == 0); pt_close();
}
static void delayed_source(void)
{
    AudioDiagnostics diagnostics; CHECK(audio_diagnostics_init(&diagnostics));
    pt_open_bytes(64 * MEMORY_KIB); pt.sink.diagnostics = &diagnostics;
    pt_track(PLAYER_LOAD, 2, true); pt_drive(); pt_heap_begin();
    for (unsigned i = 0; i < 2; ++i) { pt.readers[i].blocked = true; }
    size_t at = 0;
    for (;;) {
        size_t n = spsc_readable(&pt.sink.ring); if (n > 480) { n = 480; }
        if (!n) { break; }
        pt_render((uint32_t)n); pt_compare(pt.output, pt.files[2].golden + at * 2, n * 2); at += n;
        if (WaitForSingleObject(pt.sink.producer_event, 0) == WAIT_OBJECT_0) { pt_drive(); }
    }
    CHECK(at > 0 && at < pt.files[2].frames);
    /* No producer/network progress for three device periods. Account for every
     * missing frame explicitly, then require the exact next source sample. */
    for (unsigned i = 0; i < 3; ++i) {
        pt_render(480);
        for (unsigned j = 0; j < 960; ++j) { CHECK(pt.output[j] == 0); }
    }
    pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == at && player_snapshot(pt.player).state == PLAYER_BUFFERING);
    CHECK(player_next_wait_ms(pt.player) > 0);
    CHECK(WaitForSingleObject(pt.sink.producer_event, 0) == WAIT_TIMEOUT);
    AudioDiagnosticSnapshot view = audio_diagnostics_snapshot(&diagnostics);
    CHECK(view.values[AD_UNDERRUN_FRAMES].count == 3 && view.values[AD_UNDERRUN_FRAMES].total == 1440);
    CHECK(view.values[AD_LOW_WATER_FRAMES].count > 0 && view.values[AD_CALLBACK_TICKS].count > 3);
    for (unsigned i = 0; i < 2; ++i) {
        pt.readers[i].blocked = false;
        if (pt.readers[i].used) { pt_command((PlayerCommand){.type=PLAYER_DATA_READY,.ticket=pt.readers[i].ticket}); }
    }
    pt_drive(); pt_render(480); pt_compare(pt.output, pt.files[2].golden + at * 2, 960); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == at + 480);
    pt_command((PlayerCommand){.type=PLAYER_SEEK,.position_frames=10000}); pt_drive();
    pt_render(480); pt_compare(pt.output, pt.files[2].golden + 20000, 960); pt_drive();
    view = audio_diagnostics_snapshot(&diagnostics);
    CHECK(view.values[AD_UNDERRUN_FRAMES].total == 1440); /* Lifetime counters survive intentional seek. */
    CHECK(atomic_load(&pt.sink.underrun_frames) == 0);
    CHECK(atomic_load(&pt.sink.callback_violations) == 0);
    pt_heap_end(); pt_close();
}
void pt_stall_tests(void) { start_and_arm(); delayed_source(); }
