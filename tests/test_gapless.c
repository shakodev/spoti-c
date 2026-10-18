#include "player_test.h"

static void continuous(void)
{
    pt_open(); pt_heap_begin();
    pt_track(PLAYER_LOAD, 0, true); pt_track(PLAYER_QUEUE_APPEND, 2, false); pt_drive();
    CHECK(pt.resolved == 2 && pt.peak_readers == 2 && pt.starts == 1);
    size_t first = pt.files[0].frames, total = first + pt.files[2].frames, at = 0;
    unsigned stops = pt.stops;
    while (at < total) {
        size_t n = total - at; if (n > 137) { n = 137; }
        pt_render((uint32_t)n);
        size_t prefix = at < first ? first - at : 0; if (prefix > n) { prefix = n; }
        if (prefix) { pt_compare(pt.output, pt.files[0].golden + at * 2, prefix * 2); }
        if (n > prefix) { pt_compare(pt.output + prefix * 2, pt.files[2].golden + (at + prefix - first) * 2, (n - prefix) * 2); }
        at += n; pt_drive();
        PlayerSnapshot s = player_snapshot(pt.player);
        CHECK(s.timeline_valid);
        CHECK(s.track_id[15] == (at < first ? 1 : 3));
        CHECK(s.position_frames == (at < first ? at : at - first));
        CHECK(pt.starts == 1 && pt.stops == stops + (at == total ? 1 : 0));
        CHECK(s.state == (at == total ? PLAYER_ENDED : PLAYER_PLAYING));
    }
    CHECK(atomic_load(&pt.sink.underrun_frames) == 0 && pt.peak_readers <= 2);
    pt_heap_end(); pt_close();
}
static AudioResult never_read(void *ctx, uint64_t ticket, uint64_t offset, void *out, size_t capacity, size_t *got)
{
    (void)ctx; (void)ticket; (void)offset; (void)out; (void)capacity; (void)got; CHECK(false); return AUDIO_CORRUPT;
}
static void cancelled_prefetch(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 0, true); pt_track(PLAYER_QUEUE_APPEND, 2, false); pt_drive();
    pt_render(333); pt_drive(); uint64_t old_generation = player_snapshot(pt.player).generation;
    pt.hold_retire = true;
    pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = 10000}); pt_drive();
    CHECK(pt.held_count == 2 && player_snapshot(pt.player).state == PLAYER_SEEKING);
    for (size_t i = 0; i < 2; ++i) {
        PlayerCommand late = {.type = PLAYER_SOURCE_READY, .ticket = pt.held[i].ticket,
            .source = {1,never_read,NULL}};
        CHECK(player_submit(pt.player, &late) == PLAYER_STALE);
    }
    /* Both old sources must acknowledge retirement before new decoder slots. */
    CHECK(pt.resolved == 2);
    pt.hold_retire = false;
    for (size_t i = 0; i < pt.held_count; ++i) { pt_reply(&pt.held[i]); }
    pt.held_count = 0; pt_drive();
    CHECK(player_snapshot(pt.player).generation > old_generation && pt.resolved == 4);
    pt_render(256); pt_compare(pt.output, pt.files[0].golden + 20000, 512); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 10256);
    pt_close();
}
static void prefetch_error_boundary(void)
{
    pt_open(); pt.fail_prefetch = true;
    pt_track(PLAYER_LOAD, 0, true); pt_track(PLAYER_QUEUE_APPEND, 2, false); pt_drive();
    size_t at = 0;
    while (at < pt.files[0].frames) {
        size_t n = pt.files[0].frames - at; if (n > 256) { n = 256; }
        pt_render((uint32_t)n); pt_compare(pt.output, pt.files[0].golden + at * 2, n * 2);
        at += n; pt_drive();
        CHECK(player_snapshot(pt.player).state == (at == pt.files[0].frames ? PLAYER_ERROR : PLAYER_PLAYING));
    }
    CHECK(player_snapshot(pt.player).error == AUDIO_CORRUPT && pt.starts == 1);
    pt_close();
}
static void delayed_prefetch(void)
{
    pt_open(); pt.hold_network = true;
    pt_track(PLAYER_LOAD, 0, true); pt_track(PLAYER_QUEUE_APPEND, 2, false); pt_drive();
    CHECK(pt.held_count == 2);
    PlayerRequest next = pt.held[1]; pt_reply(&pt.held[0]); pt.held_count = 0; pt_drive();
    pt.hold_network = false;
    size_t at = 0;
    while (at < pt.files[0].frames) {
        size_t n = pt.files[0].frames - at; if (n > 512) { n = 512; }
        pt_render((uint32_t)n); pt_compare(pt.output, pt.files[0].golden + at * 2, n * 2);
        at += n; pt_drive();
    }
    pt_render(100); pt_drive();
    PlayerSnapshot s = player_snapshot(pt.player);
    CHECK(s.state == PLAYER_BUFFERING && s.position_frames == pt.files[0].frames);
    for (unsigned i = 0; i < 200; ++i) { CHECK(pt.output[i] == 0); }
    pt_reply(&next); pt_drive(); pt_render(256); pt_compare(pt.output, pt.files[2].golden, 512); pt_drive();
    s = player_snapshot(pt.player);
    CHECK(s.state == PLAYER_PLAYING && s.track_id[15] == 3 && s.position_frames == 256 && pt.starts == 1);
    pt_close();
}
static DWORD WINAPI run(void *ctx)
{
    (void)ctx; pt_load_fixtures(); continuous(); cancelled_prefetch(); prefetch_error_boundary(); delayed_prefetch();
    puts("gapless player: passed"); return 0;
}
int main(void)
{
    HANDLE t = CreateThread(NULL, 0, run, NULL, 0, NULL); CHECK(t);
    CHECK(WaitForSingleObject(t, 80000) == WAIT_OBJECT_0); CloseHandle(t); return 0;
}
