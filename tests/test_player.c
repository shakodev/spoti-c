#include "player_test.h"
#include "audio/player_internal.h"

static void command_matrix(void)
{
    pt_open();
    CHECK(player_pump(pt.player) == PLAYER_OK && player_next_wait_ms(pt.player) == INFINITE);
    HANDLE handles[3]; CHECK(player_wait_handles(pt.player, handles) == 2);
    CHECK(WaitForMultipleObjects(2, handles, FALSE, 0) == WAIT_TIMEOUT);
    pt_track(PLAYER_LOAD, 0, false); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_PAUSED && pt.starts == 0);
    pt.latency = 128; pt_command((PlayerCommand){.type = PLAYER_PLAY}); pt_drive();
    CHECK(pt.starts == 1);
    pt_render(1000); pt_drive();
    PlayerSnapshot s = player_snapshot(pt.player); CHECK(s.position_frames == 872 && s.timeline_valid);
    pt.clock_valid = false; pt_drive(); CHECK(player_snapshot(pt.player).position_frames == 872);
    pt.clock_valid = true;
    pt_command((PlayerCommand){.type = PLAYER_PAUSE}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_PAUSED && player_snapshot(pt.player).position_frames == 872);
    pt_render(1000); pt_drive(); CHECK(player_snapshot(pt.player).position_frames == 872);
    pt_command((PlayerCommand){.type = PLAYER_PLAY}); pt_drive();
    pt_render(256); pt_compare(pt.output, pt.files[0].golden + 872 * 2, 512); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 1000);
    pt.latency = 0;
    pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = 10000}); pt_drive();
    pt_render(256); pt_compare(pt.output, pt.files[0].golden + 20000, 512); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 10256);
    pt_command((PlayerCommand){.type = PLAYER_PAUSE}); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = 20000}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_PAUSED && player_snapshot(pt.player).position_frames == 20000);
    pt_command((PlayerCommand){.type = PLAYER_PLAY}); pt_drive();
    pt_render(256); pt_compare(pt.output, pt.files[0].golden + 40000, 512); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_VOLUME, .volume = -1}); pt_drive();
    CHECK(player_snapshot(pt.player).volume == 0);
    pt_render(256); for (unsigned i = 0; i < 512; ++i) { CHECK(pt.output[i] == 0); } pt_drive();
    CHECK(player_submit(pt.player, &(PlayerCommand){.type = PLAYER_VOLUME, .volume = NAN}) == PLAYER_INVALID);
    pt_command((PlayerCommand){.type = PLAYER_STOP}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_STOPPED && player_next_wait_ms(pt.player) == INFINITE);
    CHECK(player_wait_handles(pt.player, handles) == 2 && WaitForMultipleObjects(2, handles, FALSE, 0) == WAIT_TIMEOUT);
    pt_close();
}
static void underrun_and_format(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 2, true); pt_drive();
    for (unsigned i = 0; i < 2; ++i) { if (pt.readers[i].used) { pt.readers[i].blocked = true; } }
    /* Drain without refilling, then append silence. */
    for (unsigned i = 0; i < 8; ++i) { pt_render(4096); pt_drive(); }
    uint64_t before = player_snapshot(pt.player).position_frames;
    for (unsigned i = 0; i < 32; ++i) { pt_render(4096); pt_drive(); }
    PlayerSnapshot s = player_snapshot(pt.player);
    CHECK(s.position_frames < pt.files[2].frames && s.state == PLAYER_BUFFERING);
    uint64_t frozen = s.position_frames; CHECK(frozen >= before);
    pt_render(4096); pt_drive(); CHECK(player_snapshot(pt.player).position_frames == frozen);
    for (unsigned i = 0; i < 2; ++i) { pt.readers[i].blocked = false; }
    pt_drive(); pt_render(512); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == frozen + 512);
    uint64_t generation = player_snapshot(pt.player).generation, position = player_snapshot(pt.player).position_frames;
    pt.format = (AudioFormat){44100,1,AUDIO_FLOAT32}; sink_device_lost(&pt.sink); pt_drive();
    s = player_snapshot(pt.player);
    CHECK(s.rate == 44100 && s.generation > generation && pt.sink.ring.stride == sizeof(float));
    CHECK(s.position_frames == position * 44100 / 48000);
    pt_render(256); pt_drive(); CHECK(player_snapshot(pt.player).position_frames == position * 44100 / 48000 + 256);
    pt_close();
}
static DWORD WINAPI submitter(void *ctx)
{
    unsigned id = (unsigned)(uintptr_t)ctx;
    for (unsigned i = 0; i < 8; ++i) {
        PlayerCommand c = {.type = PLAYER_VOLUME, .volume = (float)(id + i) / 12};
        CHECK(player_submit(pt.player, &c) == PLAYER_OK);
        CHECK(player_snapshot(pt.player).volume >= 0);
    }
    return 0;
}
static void queue_and_mpsc(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 0, false); pt_drive();
    HANDLE threads[4];
    for (unsigned i = 0; i < 4; ++i) { threads[i] = CreateThread(NULL, 0, submitter, (void *)(uintptr_t)i, 0, NULL); CHECK(threads[i]); }
    CHECK(WaitForMultipleObjects(4, threads, TRUE, 5000) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < 4; ++i) { CloseHandle(threads[i]); }
    CHECK(player_submit(pt.player, &(PlayerCommand){.type = PLAYER_PLAY}) == PLAYER_FULL); pt_drive();
    for (unsigned i = 1; i < PLAYER_QUEUE_CAP; ++i) { pt_track(PLAYER_QUEUE_APPEND, i % 3, false); pt_drive(); }
    PlayerCommand append = {.type = PLAYER_QUEUE_APPEND}; append.track_id[15] = 1;
    CHECK(player_submit(pt.player, &append) == PLAYER_FULL);
    pt_command((PlayerCommand){.type = PLAYER_SHUFFLE, .value = 12345}); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_SHUFFLE, .value = 0}); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_REPEAT, .value = PLAYER_REPEAT_ONE}); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_NEXT}); pt_drive();
    PlayerSnapshot s = player_snapshot(pt.player); CHECK(s.track_id[15] == 2);
    pt_command((PlayerCommand){.type = PLAYER_PREVIOUS}); pt_drive();
    s = player_snapshot(pt.player); CHECK(s.track_id[15] == 1);
    pt_command((PlayerCommand){.type = PLAYER_QUEUE_CLEAR}); pt_drive();
    CHECK(player_submit(pt.player, &append) == PLAYER_OK); pt_drive();
    pt_close();
}
static void rapid_commands(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 0, true); pt_drive();
    for (unsigned n = 0; n < 50; ++n) {
        uint64_t generation = player_snapshot(pt.player).generation;
        pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = 100 + n});
        pt_command((PlayerCommand){.type = PLAYER_PAUSE});
        pt_track(PLAYER_LOAD, n % 3, true);
        pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = 200 + n});
        pt_command((PlayerCommand){.type = PLAYER_PLAY}); pt_drive();
        PlayerSnapshot s = player_snapshot(pt.player);
        CHECK(s.generation > generation && s.track_id[15] == n % 3 + 1 && s.state == PLAYER_PLAYING);
        CHECK(pt.peak_readers <= 2);
    }
    pt_close();
}
static void shutdown_and_errors(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 0, true); pt_drive();
    pt.hold_retire = true;
    pt_command((PlayerCommand){.type = PLAYER_SHUTDOWN}); pt_drive();
    CHECK(player_submit(pt.player, &(PlayerCommand){.type = PLAYER_PLAY}) == PLAYER_CLOSED);
    CHECK(player_destroy(pt.player) == PLAYER_BAD_STATE && pt.held_count == 1);
    CHECK(player_submit(pt.player, &(PlayerCommand){.type = PLAYER_SOURCE_FAILED, .ticket = UINT64_MAX}) == PLAYER_STALE);
    pt.hold_retire = false; pt_reply(&pt.held[0]); pt.held_count = 0; pt_drive();
    CHECK(player_pump(pt.player) == PLAYER_CLOSED);
    /* Final helper shutdown is already admitted, so finish explicitly here. */
    CHECK(player_destroy(pt.player) == PLAYER_OK); pt.player = NULL;
    CHECK(sink_close(&pt.sink) == AUDIO_OK);
    for (unsigned i = 0; i < 2; ++i) { arena_dispose(&pt.codec[i]); arena_dispose(&pt.work[i]); }
    arena_dispose(&pt.core);

    pt_open(); pt.hold_network = true; pt_track(PLAYER_LOAD, 0, true); pt_drive();
    CHECK(pt.held_count == 1);
    pt_command((PlayerCommand){.type = PLAYER_SOURCE_FAILED, .ticket = pt.held[0].ticket, .error = AUDIO_UNSUPPORTED});
    pt.hold_network = false; pt.held_count = 0; pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_ERROR && player_snapshot(pt.player).error == AUDIO_UNSUPPORTED);
    pt_close();
}
static void timeline_ledger(void)
{
    pt_open(); CHECK(sink_start(&pt.sink) == AUDIO_OK); pt.latency = 2;
    float data[16]; for (unsigned i = 0; i < 16; ++i) { data[i] = 0.25f; }
    size_t written; CHECK(sink_write(&pt.sink, data, 4, &written) == AUDIO_OK);
    pt_render(8); SinkInfo info = sink_info(&pt.sink);
    CHECK(info.media_frames == 4 && info.played_media_frames == 4 && info.timeline_valid);
    CHECK(sink_write(&pt.sink, data, 3, &written) == AUDIO_OK); pt_render(3);
    info = sink_info(&pt.sink); CHECK(info.media_frames == 7 && info.played_media_frames == 5);
    pt.clock_valid = false; info = sink_info(&pt.sink); CHECK(info.played_media_frames == 5 && info.timeline_valid);
    for (unsigned n = 0; n < 1000; ++n) { pt_render(2); }
    info = sink_info(&pt.sink);
    CHECK(info.played_media_frames == 7 && info.timeline_valid && !info.timeline_overflow);
    pt_close();

    pt_open(); pt_track(PLAYER_LOAD, 2, true); pt_drive();
    for (unsigned i = 0; i <= SINK_TIMELINE_SPANS; ++i) { pt_render(1); }
    pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_ERROR && player_snapshot(pt.player).error == AUDIO_LIMIT);
    pt_close();
}
static DWORD WINAPI wrong_owner(void *ctx)
{
    Player *p = ctx; HANDLE handles[3];
    CHECK(player_pump(p) == PLAYER_BAD_STATE && player_destroy(p) == PLAYER_BAD_STATE);
    CHECK(player_wait_handles(p, handles) == 0 && player_next_wait_ms(p) == INFINITE); return 0;
}
static void queue_policy(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 0, false); pt_track(PLAYER_QUEUE_APPEND, 1, false);
    pt_track(PLAYER_QUEUE_APPEND, 2, false); pt_drive();
    Player *p = pt.player; unsigned next;
    CHECK(player_next_index(p, 0, true, &next) && next == 1);
    CHECK(!player_next_index(p, 2, true, &next));
    p->repeat = PLAYER_REPEAT_ONE;
    CHECK(player_next_index(p, 0, true, &next) && next == 0);
    CHECK(player_next_index(p, 0, false, &next) && next == 1);
    p->repeat = PLAYER_REPEAT_ALL; CHECK(player_next_index(p, 2, true, &next) && next == 0);
    player_queue_shuffle(p, 12345); unsigned expected[3]; memcpy(expected, p->order, sizeof(expected));
    player_queue_shuffle(p, 12345); CHECK(memcmp(expected, p->order, sizeof(expected)) == 0);
    unsigned mask = 0; for (unsigned i = 0; i < 3; ++i) { mask |= 1u << p->order[i]; } CHECK(mask == 7);
    player_queue_shuffle(p, 0); CHECK(p->order[0] == 0 && p->order[1] == 1 && p->order[2] == 2);
    HANDLE thread = CreateThread(NULL, 0, wrong_owner, p, 0, NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread);
    pt_close();
}
static void seek_limits(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 0, true); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = pt.files[0].frames}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_ENDED);
    CHECK(player_snapshot(pt.player).position_frames == pt.files[0].frames);
    pt_track(PLAYER_LOAD, 0, false); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = pt.files[0].frames}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_PAUSED);
    CHECK(player_snapshot(pt.player).position_frames == pt.files[0].frames);
    pt_command((PlayerCommand){.type = PLAYER_PLAY}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_ENDED);
    pt_track(PLAYER_LOAD, 0, true); pt_drive();
    pt_command((PlayerCommand){.type = PLAYER_SEEK, .position_frames = UINT64_MAX}); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_ERROR && player_snapshot(pt.player).error == AUDIO_LIMIT);
    pt_close();
}
static void unavailable_device(void)
{
    pt_open(); pt_track(PLAYER_LOAD, 0, true); pt_drive();
    pt.format.channels = 3; sink_device_lost(&pt.sink); pt_drive();
    CHECK(player_snapshot(pt.player).state == PLAYER_ERROR && !pt.sink.opened);
    HANDLE handles[3]; CHECK(player_wait_handles(pt.player, handles) == 1);
    CHECK(handles[0] && WaitForSingleObject(handles[0], 0) == WAIT_TIMEOUT);
    pt_close();
}
static DWORD WINAPI run(void *ctx)
{
    (void)ctx; pt_load_fixtures(); DWORD before, after; CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    command_matrix(); underrun_and_format(); queue_and_mpsc(); rapid_commands();
    shutdown_and_errors(); timeline_ledger(); queue_policy(); seek_limits(); unavailable_device();
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after)); CHECK(before == after);
    puts("player: passed"); return 0;
}
int main(void)
{
    HANDLE t = CreateThread(NULL, 0, run, NULL, 0, NULL); CHECK(t);
    CHECK(WaitForSingleObject(t, 80000) == WAIT_OBJECT_0); CloseHandle(t); return 0;
}
