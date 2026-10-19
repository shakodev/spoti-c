#include "lyrics_test.h"
#include "player_test.h"

static uint64_t fake_now;
static uint64_t fake_read(void *context) { return *(uint64_t *)context; }
static LyricsView update(LyricsTimeline *timeline)
{
    PlayerSnapshot snapshot = player_snapshot(pt.player);
    return lyrics_timeline_update(timeline, &snapshot, true, fake_now);
}

void lyrics_timeline_tests(void)
{
    LyricsRequest request = lt_request(2000, 3); const LyricsDocument *doc;
    CHECK(lt_deliver(request, LYRICS_BODY("LINE_SYNCED","NONE",
        "[{\"startTimeMs\":\"0\",\"words\":\"zero\"},{\"startTimeMs\":\"10\",\"words\":\"a\"},"
        "{\"startTimeMs\":\"10\",\"words\":\"b\"},{\"startTimeMs\":\"20\",\"words\":\"twenty\"},"
        "{\"startTimeMs\":\"100\",\"words\":\"hundred\"},{\"startTimeMs\":\"1000\",\"words\":\"later\"},"
        "{\"startTimeMs\":\"2000\",\"words\":\"final\"}]", "[]"), &doc) == LYRICS_OK);
    LyricsTimeline timeline = {0}; CHECK(lyrics_timeline_set(&timeline, doc));
    CHECK(lyrics_document_release(doc) && lyrics_reset(lt_service, 2001) == LYRICS_OK);
    fake_now = 100; CoreClock clock = {fake_read, &fake_now};
    PlayerSnapshot s = {.rate = 44100, .position_frames = 440, .state = PLAYER_PLAYING, .timeline_valid = true}; s.track_id[15] = 3;
    CHECK(lyrics_next_deadline(doc, &s, true, core_clock_now(&clock)) == 101);
    s.position_frames = 441;
    CHECK(lyrics_timeline_update(&timeline, &s, true, fake_now).line == 2);
    CHECK(lyrics_next_deadline(doc, &s, true, fake_now) == 110);
    s.position_frames = 88200;
    CHECK(lyrics_timeline_update(&timeline, &s, true, fake_now).line == 6);
    CHECK(lyrics_next_deadline(doc, &s, true, fake_now) == CORE_CLOCK_NEVER);
    s.position_frames = 0;
    CHECK(lyrics_next_deadline(doc, &s, true, UINT64_MAX - 1) == CORE_CLOCK_NEVER);
    s.rate = 1; s.position_frames = UINT64_MAX;
    CHECK(!lyrics_timeline_update(&timeline, &s, true, fake_now).valid);
    s.rate = 48000; s.position_frames = 0; s.track_id[15] = 1;
    CHECK(!lyrics_timeline_update(&timeline, &s, true, fake_now).valid);

    pt_load_fixtures(); pt_open(); pt_track(PLAYER_LOAD, 2, true); pt_drive();
    CHECK(update(&timeline).line == 0 && update(&timeline).deadline_ms == fake_now + 10);
    pt.latency = 480; pt_render(720); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 240);
    CHECK(update(&timeline).line == 0 && update(&timeline).deadline_ms == fake_now + 5);
    pt.latency = 720; pt_render(240); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 240 && update(&timeline).line == 0);
    pt.latency = 240; pt_render(240); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 960 && update(&timeline).line == 3);
    /* Slow hardware clock drift, then recovery. No UI clock extrapolation. */
    pt.output_frames -= 48; pt_render(96); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 1008 && update(&timeline).line == 3);
    pt.clock_valid = false; pt_drive(); /* Latency fallback remains authoritative. */
    CHECK(update(&timeline).line == 3); pt.clock_valid = true;
    pt_command((PlayerCommand){.type = PLAYER_PAUSE}); pt_drive();
    uint64_t paused = player_snapshot(pt.player).position_frames; LyricsView view = update(&timeline);
    CHECK(view.line == 3 && view.deadline_ms == CORE_CLOCK_NEVER);
    pt_heap_begin();
    for (unsigned i = 0; i < 200; ++i) {
        fake_now += 1000; s = player_snapshot(pt.player);
        CHECK(s.position_frames == paused);
        CHECK(lyrics_next_deadline(doc, &s, true, core_clock_now(&clock)) == CORE_CLOCK_NEVER);
        CHECK(lyrics_next_deadline(doc, &s, false, core_clock_now(&clock)) == CORE_CLOCK_NEVER);
    }
    pt_heap_end();
    pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = 480}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_PAUSED && update(&timeline).line == 2);
    pt.latency = 0; pt_command((PlayerCommand){.type = PLAYER_PLAY}); pt_drive(); pt_render(480); pt_drive();
    CHECK(update(&timeline).line == 3);
    s = player_snapshot(pt.player);
    CHECK(lyrics_next_deadline(doc, &s, false, fake_now) == CORE_CLOCK_NEVER);
    s.timeline_valid = false; CHECK(lyrics_next_deadline(doc, &s, true, fake_now) == CORE_CLOCK_NEVER);

    for (unsigned i = 0; i < 2; ++i) { if (pt.readers[i].used) { pt.readers[i].blocked = true; } }
    for (unsigned i = 0; i < 40; ++i) { pt_render(4096); pt_drive(); }
    s = player_snapshot(pt.player); CHECK(s.state == PLAYER_BUFFERING);
    uint64_t frozen = s.position_frames; uint32_t line = update(&timeline).line;
    for (unsigned i = 0; i < 10; ++i) {
        fake_now += 1000; pt_render(4096); pt_drive();
        CHECK(player_snapshot(pt.player).position_frames == frozen);
        CHECK(update(&timeline).line == line && update(&timeline).deadline_ms == CORE_CLOCK_NEVER);
    }
    for (unsigned i = 0; i < 2; ++i) { pt.readers[i].blocked = false; }
    pt_drive(); pt_render(512); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == frozen + 512);
    pt.format = (AudioFormat){44100,1,AUDIO_FLOAT32}; sink_device_lost(&pt.sink); pt_drive(); pt_render(256); pt_drive();
    s = player_snapshot(pt.player); CHECK(s.rate == 44100 && update(&timeline).valid);
    CHECK(update(&timeline).line == lyrics_line_at(doc, s.position_frames * 1000 / s.rate));
    pt_track(PLAYER_LOAD, 0, true); pt_drive();
    CHECK(!update(&timeline).valid && update(&timeline).deadline_ms == CORE_CLOCK_NEVER);
    pt_close(); CHECK(lyrics_timeline_set(&timeline, NULL));
    CHECK(json_page_pool_resident(lt_pool) == 0);
}
