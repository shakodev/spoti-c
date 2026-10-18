#include "connect_test.h"
#include "protocol/connect_internal.h"
#include "pb_decode.h"

static _Alignas(16) unsigned char state_memory[65536], wire[DEALER_POOL_BYTES], scratch[DEALER_POOL_BYTES];
static Arena state_arena;
static ConnectState *state;
void connect_http_tests(void);
static bool next_track(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void)field; unsigned *count = *arg;
    spotify_connectstate_ProvidedTrack track = spotify_connectstate_ProvidedTrack_init_zero;
    if (!pb_decode(stream, spotify_connectstate_ProvidedTrack_fields, &track)) { return false; }
    CHECK(!strcmp(track.uri, "spotify:track:0000000000000000000002")); ++*count; return true;
}
static DealerResult command(const char *name)
{
    size_t n = connect_fixture(name, wire, DEALER_PAYLOAD_CAP);
    return connect_command(state, (char *)wire, n, scratch);
}
static DealerResult message(const char *kind)
{
    char file[64], uri[128]; sprintf_s(file, sizeof(file), "%s.pb", kind);
    size_t n = connect_fixture(file, wire, sizeof(wire));
    sprintf_s(uri, sizeof(uri), "hm://connect-state/v1/%s%s", !strcmp(kind, "cluster") ? "" : "connect/", kind);
    return connect_message(state, uri, wire, n);
}
static void open_state(void)
{
    pt_open(); CHECK(arena_init(&state_arena, state_memory, sizeof(state_memory)));
    ConnectConfig cfg = {.player = pt.player, .device_id = "fixture-device", .name = "Fixture Player",
        .client_id = "fixture-client", .fixture_replay = true};
    CHECK(connect_create(&state_arena, &cfg, &state) == DEALER_OK);
    CHECK(connect_connection(state, "fixture-connection") == DEALER_OK);
}
static void close_state(void)
{
    CHECK(connect_shutdown(state) == DEALER_OK); arena_dispose(&state_arena); pt_close();
}
static void controls(void)
{
    open_state(); CHECK(command("transfer.json") == DEALER_OK); pt_drive();
    PlayerSnapshot p = player_snapshot(pt.player); CHECK(p.state == PLAYER_PAUSED && p.track_id[15] == 1);
    CHECK(p.position_frames == 5760); uint64_t generation = p.generation;
    CHECK(command("transfer.json") == DEALER_OK); pt_drive(); CHECK(player_snapshot(pt.player).generation == generation);
    CHECK(command("resume.json") == DEALER_OK); pt_drive(); pt_render(256); pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_PLAYING);
    CHECK(command("pause.json") == DEALER_OK); pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_PAUSED);
    CHECK(command("seek_to.json") == DEALER_OK);
    pt.format = (AudioFormat){44100,2,AUDIO_FLOAT32}; sink_device_lost(&pt.sink); pt_drive();
    CHECK(player_snapshot(pt.player).position_frames == 8820);
    pt.format = (AudioFormat){48000,2,AUDIO_FLOAT32}; sink_device_lost(&pt.sink); pt_drive();
    CHECK(command("skip_next.json") == DEALER_OK); pt_drive(); p = player_snapshot(pt.player); CHECK(p.track_id[15] == 2);
    CHECK(command("skip_prev.json") == DEALER_OK); pt_drive(); p = player_snapshot(pt.player); CHECK(p.track_id[15] == 1);
    CHECK(command("unknown.json") == DEALER_UNSUPPORTED);
    CHECK(message("volume") == DEALER_OK); pt_drive(); CHECK(fabsf(player_snapshot(pt.player).volume - 0.5f) < 0.001f);
    CHECK(message("cluster") == DEALER_OK); CHECK(connect_snapshot(state).active);
    CHECK(connect_snapshot(state).controls == 127);
    CHECK(command("play.json") == DEALER_OK); pt_drive(); pt_render(256); pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_PLAYING);
    CHECK(message("logout") == DEALER_CLOSED); pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_STOPPED);
    CHECK(connect_snapshot(state).logged_out && command("play.json") == DEALER_CLOSED);
    close_state();
}
static void limits(void)
{
    open_state();
    for (unsigned i = 0; i < PLAYER_COMMAND_CAP; ++i) { pt_command((PlayerCommand){.type = PLAYER_VOLUME, .volume = 1}); }
    CHECK(command("transfer.json") == DEALER_FULL); pt_drive(); CHECK(player_snapshot(pt.player).state == PLAYER_STOPPED);
    CHECK(command("transfer.json") == DEALER_OK); CHECK(command("play.json") == DEALER_FULL);
    pt_drive(); CHECK(command("play.json") == DEALER_OK); pt_drive();
    uint64_t generation = player_snapshot(pt.player).generation;
    const char *bad[] = {
        "{}", "{\"message_id\":-1,\"sent_by_device_id\":\"x\",\"command\":{\"endpoint\":\"pause\"}}",
        "{\"message_id\":1,\"sent_by_device_id\":\"x\\u0000y\",\"command\":{\"endpoint\":\"pause\"}}",
        "{\"message_id\":2,\"sent_by_device_id\":\"x\",\"command\":{\"endpoint\":\"seek_to\",\"value\":4294967296}}",
        "{\"message_id\":3,\"sent_by_device_id\":\"x\",\"command\":{\"endpoint\":\"transfer\",\"data\":\"!!!!\"}}"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        strcpy_s((char *)wire, sizeof(wire), bad[i]);
        CHECK(connect_command(state, (char *)wire, strlen(bad[i]), scratch) == DEALER_MALFORMED);
    }
    pt_drive(); CHECK(player_snapshot(pt.player).generation == generation);
    uint8_t id[16]; CHECK(!connect_track_uri("spotify:track:ZZZZZZZZZZZZZZZZZZZZZZ", id));
    CHECK(connect_track_uri("spotify:track:0000000000000000000001", id) && id[15] == 1);
    /* Queue overflow and invalid data fail before player admission. */
    PlayerTransfer transfer = {.count = PLAYER_QUEUE_CAP + 1};
    CHECK(player_submit(pt.player, &(PlayerCommand){.type = PLAYER_TRANSFER, .transfer = &transfer}) == PLAYER_INVALID);
    size_t n = connect_fixture("transfer.pb", wire, sizeof(wire));
    for (size_t i = 0; i < n; ++i) { CHECK(connect_transfer(wire, i, &transfer) != DEALER_OK || i > 40); }
    CHECK(connect_transfer(wire, n, &transfer) == DEALER_OK && transfer.count == 2);
    n = connect_fixture("context-transfer.pb", wire, sizeof(wire));
    CHECK(connect_transfer(wire, n, &transfer) == DEALER_OK && transfer.count == 2 && transfer.ids[1][15] == 2 && transfer.position_ms == 120);
    n = connect_fixture("paginated-transfer.pb", wire, sizeof(wire));
    CHECK(connect_transfer(wire, n, &transfer) == DEALER_UNSUPPORTED);
    n = connect_fixture("queue-overflow.pb", wire, sizeof(wire));
    CHECK(connect_transfer(wire, n, &transfer) != DEALER_OK);
    n = connect_fixture("playing-queue.pb", wire, sizeof(wire));
    CHECK(connect_transfer(wire, n, &transfer) == DEALER_OK && transfer.count == 2 && transfer.ids[0][15] == 2 && transfer.ids[1][15] == 1);
    const char *duplicate = "{\"message_id\":1,\"message_id\":2,\"sent_by_device_id\":\"x\",\"command\":{\"endpoint\":\"pause\"}}";
    strcpy_s((char *)wire, sizeof(wire), duplicate);
    CHECK(connect_command(state, (char *)wire, strlen(duplicate), scratch) == DEALER_MALFORMED);
    const char *album = "{\"message_id\":99,\"sent_by_device_id\":\"x\",\"command\":{\"endpoint\":\"play\",\"context\":{\"uri\":\"spotify:album:fixture\"},\"options\":{\"skip_to\":{\"track_uri\":\"spotify:track:0000000000000000000001\"}}}}";
    strcpy_s((char *)wire, sizeof(wire), album);
    CHECK(connect_command(state, (char *)wire, strlen(album), scratch) == DEALER_UNSUPPORTED);
    CHECK(connect_connection(state, NULL) == DEALER_OK && command("pause.json") == DEALER_PENDING);
    close_state();
}
static void registration(void)
{
    open_state(); size_t n;
    CHECK(command("transfer.json") == DEALER_OK); pt_drive();
    CHECK(WaitForSingleObject(player_state_event(pt.player), 0) == WAIT_OBJECT_0);
    PlayerQueueSnapshot queue; player_take_state(pt.player, &queue);
    CHECK(queue.count == 2 && queue.index == 0 && queue.ids[0][15] == 1 && queue.ids[1][15] == 2);
    CHECK(WaitForSingleObject(player_state_event(pt.player), 0) == WAIT_TIMEOUT);
    pt_drive(); CHECK(WaitForSingleObject(player_state_event(pt.player), 0) == WAIT_TIMEOUT);
    CHECK(connect_encode(state, wire, sizeof(wire), &n) == DEALER_OK);
    spotify_connectstate_PutStateRequest req = spotify_connectstate_PutStateRequest_init_zero;
    unsigned next = 0; req.device.player_state.next_tracks.funcs.decode = next_track; req.device.player_state.next_tracks.arg = &next;
    CHECK(proto_decode(spotify_connectstate_PutStateRequest_fields, wire, n, &req) == PROTO_OK);
    CHECK(req.member_type == spotify_connectstate_MemberType_CONNECT_STATE && req.has_device);
    spotify_connectstate_Capabilities *c = &req.device.device_info.capabilities;
    CHECK(c->supports_gzip_pushes && c->supports_transfer_command && c->command_acks && c->is_controllable);
    CHECK(!c->supports_set_options_command && !c->supports_rooms && !c->supports_dj && !c->supports_external_episodes && !c->supports_rename);
    CHECK(!strcmp(req.device.device_info.device_id, "fixture-device"));
    CHECK(next == 1 && req.device.player_state.position_as_of_timestamp == 120 && req.device.player_state.is_paused);
    CHECK(connect_encode(state, wire, 1, &n) == DEALER_LIMIT);
    close_state();
}
static DWORD WINAPI run(void *context)
{
    (void)context; DWORD before, after; CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    pt_load_fixtures(); controls(); limits(); registration();
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after) && before == after);
    connect_http_tests();
    puts("Connect command, transfer, deduplication, capability and cleanup tests passed"); return 0;
}
int main(void)
{
    HANDLE thread = CreateThread(NULL, 0, run, NULL, 0, NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread, 80000) == WAIT_OBJECT_0); CloseHandle(thread); return 0;
}
