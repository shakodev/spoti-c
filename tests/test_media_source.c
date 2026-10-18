#include <winsock2.h>
#include "media_test.h"
#include "protocol/media_internal.h"
#include "support/http_server.h"
#include <stdatomic.h>
#include <wchar.h>

static const char uri[] = "spotify:track:0000000000000000000000";
static _Alignas(16) uint8_t media_memory[MEMORY_BUDGET_METADATA], http_memory[MEMORY_BUDGET_HTTP], key_memory[4096];
static struct {
    Arena media_arena, http_arena, key_arena;
    Metrics metrics; MetricsArena bindings[3];
    HttpClient *http; AudioKeyClient *keys; MediaResolver *media;
    TestHttpServer server; TestHttpResponse replies[8]; char headers[8][180];
    wchar_t origin[128];
    uint64_t now, utc; size_t key_sends, answered, callbacks, initial_use;
    uint8_t key_request[42]; MediaResult result;
    ApPacket local, remote;
} fixture;
static MediaWire metadata, storage, wire;

static uint64_t now(void *ctx) { (void)ctx; return fixture.now; }
static uint64_t utc(void *ctx) { (void)ctx; return fixture.utc; }
static ApResult send_key(void *ctx, uint8_t command, const void *data, size_t bytes)
{
    (void)ctx; CHECK(command == AUDIO_KEY_REQUEST && bytes == 42);
    memcpy(fixture.key_request, data, bytes); ++fixture.key_sends; return AP_OK;
}
static void completed(void *ctx, MediaId id, MediaResult result)
{
    (void)ctx; ++fixture.callbacks; fixture.result = result;
    CHECK(media_source_retire(fixture.media, id) == MEDIA_BAD_STATE);
}

static void make_metadata(const char *request_uri, uint8_t gid_last, bool incomplete_alternative)
{
    MediaWire track = {0}, file = {0}, any = {0}, entity = {0}, array = {0}, header = {0};
    uint8_t gid[16] = {0}, file_id[20]; gid[15] = gid_last; memset(file_id, 0x42, sizeof(file_id));
    media_blob(&track, 1, gid, sizeof(gid)); media_number(&track, 7, 420000);
    if (incomplete_alternative) {
        MediaWire alternative = {0}; gid[15] = 1;
        media_blob(&alternative, 1, gid, sizeof(gid)); media_message(&track, 13, &alternative);
    } else {
        media_blob(&file, 1, file_id, sizeof(file_id)); media_number(&file, 2, 2); media_message(&track, 12, &file);
    }
    media_text(&any, 1, "type.googleapis.com/spotify.metadata.Track"); media_message(&any, 2, &track);
    media_number(&header, 1, 200); media_message(&entity, 1, &header);
    media_text(&entity, 2, request_uri); media_message(&entity, 3, &any);
    media_number(&array, 2, 10); media_message(&array, 3, &entity);
    metadata.bytes = 0; media_message(&metadata, 2, &array);
}
static void make_storage(void)
{
    storage.bytes = 0; uint8_t file_id[20]; memset(file_id, 0x42, sizeof(file_id));
    media_blob(&storage, 4, file_id, sizeof(file_id));
    media_text(&storage, 2, "https://cdn.example/audio?verify=2000000125-fixture");
}
static void open_fixture(const MediaWire *const *messages, size_t count, bool hold_body)
{
    memset(&fixture, 0, sizeof(fixture)); fixture.now = 1000; fixture.utc = UINT64_C(2000000000000);
    CHECK(metrics_init(&fixture.metrics));
    for (size_t i = 0; i < count; ++i) {
        CHECK(sprintf_s(fixture.headers[i], sizeof(fixture.headers[i]),
            "HTTP/1.1 200 OK\r\nContent-Type: application/protobuf\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", messages[i]->bytes) > 0);
        fixture.replies[i] = (TestHttpResponse){.headers = fixture.headers[i], .body = messages[i]->data,
            .bytes = messages[i]->bytes, .fragment = 5, .capture_upload = true, .hold_body = hold_body};
    }
    CHECK(test_http_server_start(&fixture.server, &fixture.metrics, fixture.replies, count));
    CHECK(swprintf_s(fixture.origin, 128, L"http://127.0.0.1:%u", (unsigned)fixture.server.port) > 0);
    CHECK(arena_init(&fixture.media_arena, media_memory, sizeof(media_memory)));
    CHECK(arena_init(&fixture.http_arena, http_memory, sizeof(http_memory)));
    CHECK(arena_init(&fixture.key_arena, key_memory, sizeof(key_memory)));
    CHECK(metrics_attach_arena(&fixture.metrics, &fixture.media_arena, &fixture.bindings[0]));
    CHECK(metrics_attach_arena(&fixture.metrics, &fixture.http_arena, &fixture.bindings[1]));
    CHECK(metrics_attach_arena(&fixture.metrics, &fixture.key_arena, &fixture.bindings[2]));
    HttpConfig hc = {.metrics = &fixture.metrics, .allow_loopback_http = true, .header_origin_count = 1};
    hc.header_origins[0] = fixture.origin;
    CHECK(http_client_create(&fixture.http_arena, &hc, &fixture.http) == HTTP_OK);
    AudioKeyConfig kc = {.send = send_key, .clock = {now, NULL}};
    CHECK(audio_key_create(&fixture.key_arena, &kc, &fixture.keys) == AP_OK && audio_key_connect(fixture.keys, 1) == AP_OK);
    MediaConfig mc = {.http = fixture.http, .keys = fixture.keys, .fixture_origin = fixture.origin,
        .clock = {now, NULL}, .utc_ms = utc, .policy = {{'N','L',0}, "premium", false, 0}};
    CHECK(media_resolver_create(&fixture.media_arena, &mc, &fixture.media) == MEDIA_OK);
    fixture.initial_use = fixture.media_arena.used;
    uint8_t key[32] = {0};
    CHECK(ap_packet_init(&fixture.local, key, key) == AP_OK && ap_packet_init(&fixture.remote, key, key) == AP_OK);
}
static void answer_key(void)
{
    uint8_t body[20]; memcpy(body, fixture.key_request + 36, 4); memset(body + 4, 0xa5, 16);
    CHECK(ap_packet_send(&fixture.remote, AUDIO_KEY_RESPONSE, body, sizeof(body)) == AP_OK);
    size_t used; CHECK(ap_packet_feed(&fixture.local, fixture.remote.tx, fixture.remote.tx_bytes, &used) == AP_OK);
    CHECK(audio_key_on_packet(fixture.keys, 1, &fixture.local) == AP_OK);
    CHECK(ap_packet_advance(&fixture.remote, used) == AP_OK && ap_packet_consume(&fixture.local) == AP_OK);
}
static MediaResult drive(MediaId id, MediaSource *source)
{
    uint64_t until = GetTickCount64() + 8000;
    for (;;) {
        CHECK(GetTickCount64() < until);
        CHECK(media_source_poll(fixture.media) == MEDIA_OK);
        if (fixture.key_sends > fixture.answered) { answer_key(); fixture.answered = fixture.key_sends; }
        MediaResult result = media_source_get(fixture.media, id, source);
        if (result != MEDIA_PENDING && result != MEDIA_EXPIRED) { return result; }
        if (fixture.media->active_slot >= 0) { CHECK(http_wait(fixture.http, 1000) == HTTP_OK); }
    }
}
static void close_fixture(void)
{
    uint64_t until = GetTickCount64() + 5000;
    while (media_resolver_shutdown(fixture.media) == MEDIA_PENDING) {
        CHECK(GetTickCount64() < until && http_wait(fixture.http, 1000) == HTTP_OK);
    }
    CHECK(media_resolver_balanced(fixture.media));
    CHECK(audio_key_disconnect(fixture.keys, false) == AP_OK);
    CHECK(fixture.media_arena.used == fixture.initial_use);
    CHECK(http_client_shutdown(fixture.http, 5000) == HTTP_OK); test_http_server_stop(&fixture.server);
    ap_packet_clear(&fixture.local); ap_packet_clear(&fixture.remote);
    arena_dispose(&fixture.media_arena); arena_dispose(&fixture.http_arena); arena_dispose(&fixture.key_arena);
    CHECK(metrics_balanced(&fixture.metrics));
}

static void ids(void)
{
    uint8_t gid[16], expected[16], before[16]; char encoded[MEDIA_URI_BYTES], hex[41];
    CHECK(media_gid_from_base62("0000000000000000000001", gid) == MEDIA_OK && gid[15] == 1);
    CHECK(media_gid_from_base62("4uLU6hMCjMI75M1A2tKUQC", gid) == MEDIA_OK);
    CHECK(media_id_from_hex("93bc414a606747b2b612491ef83d5a3e", 16, expected) == MEDIA_OK && memcmp(gid, expected, 16) == 0);
    CHECK(media_gid_to_uri(gid, encoded) == MEDIA_OK && strcmp(encoded + 14, "4uLU6hMCjMI75M1A2tKUQC") == 0);
    CHECK(media_id_from_hex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", 16, expected) == MEDIA_OK);
    CHECK(media_gid_to_uri(expected, encoded) == MEDIA_OK && media_gid_from_uri(encoded, gid) == MEDIA_OK);
    CHECK(memcmp(gid, expected, 16) == 0); memcpy(before, gid, 16);
    CHECK(media_gid_from_base62("ZZZZZZZZZZZZZZZZZZZZZZ", gid) == MEDIA_LIMIT && memcmp(gid, before, 16) == 0);
    CHECK(media_gid_from_uri("spotify:episode:0000000000000000000000", gid) == MEDIA_INVALID);
    CHECK(media_gid_from_base62("000000000000000000000!", gid) == MEDIA_INVALID);
    CHECK(media_id_from_hex("00112233445566778899aabbccddeeff", 16, gid) == MEDIA_OK);
    CHECK(media_id_to_hex(gid, 16, hex, sizeof(hex)) == MEDIA_OK && strcmp(hex, "00112233445566778899aabbccddeeff") == 0);
    CHECK(media_id_from_hex("00112233445566778899aabbccddeefg", 16, gid) == MEDIA_INVALID);
}

static void successful_source_and_leases(void)
{
    make_metadata(uri, 0, false); make_storage(); const MediaWire *messages[] = {&metadata, &storage};
    open_fixture(messages, 2, false); MediaId id;
    CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &id) == MEDIA_OK);
    MediaSource source; CHECK(drive(id, &source) == MEDIA_OK && fixture.callbacks == 1 && fixture.result == MEDIA_OK);
    CHECK(strcmp(source.original_uri, uri) == 0 && source.track.format == 2 && source.track.duration_ms == 210000);
    CHECK(fixture.key_sends == 1 && memcmp(fixture.key_request, source.track.file_id, 20) == 0);
    CHECK(memcmp(fixture.key_request + 20, source.track.gid, 16) == 0);
    CHECK(strstr(fixture.server.requests[0], "POST /extended-metadata/v0/extended-metadata HTTP/1.1") != NULL);
    CHECK(strstr(fixture.server.requests[0], "Content-Type: application/protobuf") != NULL);
    CHECK(strstr(fixture.server.requests[1],
        "GET /storage-resolve/v2/files/audio/interactive/2/4242424242424242424242424242424242424242?product=0 HTTP/1.1") != NULL);
    CHECK(strstr(fixture.server.requests[1], "Authorization:") == NULL && strstr(fixture.server.requests[1], "client-token:") == NULL);
    CHECK(media_source_next_wait_ms(fixture.media) == 60000);
    MediaKeyLease lease = {0}, second = {0};
    CHECK(media_key_acquire(fixture.media, source.key, &lease) == MEDIA_OK);
    CHECK(media_key_acquire(fixture.media, source.key, &second) == MEDIA_OK);
    MediaKeyLease extra[MEDIA_LEASES - 2] = {0}, overflow = {0};
    for (size_t i = 0; i < MEDIA_LEASES - 2; ++i) { CHECK(media_key_acquire(fixture.media, source.key, &extra[i]) == MEDIA_OK); }
    CHECK(media_key_acquire(fixture.media, source.key, &overflow) == MEDIA_FULL);
    const uint8_t *secret = lease.key; MediaKeyLease duplicate = lease;
    CHECK(media_source_retire(fixture.media, id) == MEDIA_OK && secret[0] == 0xa5);
    CHECK(media_source_get(fixture.media, id, &source) == MEDIA_STALE);
    CHECK(media_key_acquire(fixture.media, id, &(MediaKeyLease){0}) == MEDIA_STALE);
    CHECK(media_key_release(fixture.media, &lease) == MEDIA_OK && secret[0] == 0xa5);
    CHECK(media_key_release(fixture.media, &duplicate) == MEDIA_STALE);
    for (size_t i = 0; i < MEDIA_LEASES - 2; ++i) { CHECK(media_key_release(fixture.media, &extra[i]) == MEDIA_OK); }
    CHECK(media_key_release(fixture.media, &second) == MEDIA_OK);
    for (size_t i = 0; i < 16; ++i) { CHECK(secret[i] == 0); }
    MediaId fresh; CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &fresh) == MEDIA_OK);
    CHECK(fresh.generation != id.generation && media_source_retire(fixture.media, id) == MEDIA_STALE);
    CHECK(media_source_retire(fixture.media, fresh) == MEDIA_OK && fixture.callbacks == 2);
    close_fixture();
}

static void refresh_and_alternatives(void)
{
    make_metadata(uri, 0, false); make_storage(); const MediaWire *messages[] = {&metadata, &storage, &storage};
    open_fixture(messages, 3, false); MediaId id; MediaSource source;
    CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &id) == MEDIA_OK && drive(id, &source) == MEDIA_OK);
    fixture.now += 60000; CHECK(media_source_get(fixture.media, id, &source) == MEDIA_EXPIRED);
    CHECK(drive(id, &source) == MEDIA_OK && source.urls[0].refresh_at == fixture.now + 60000);
    CHECK(fixture.callbacks == 1 && fixture.key_sends == 1 && atomic_load(&fixture.server.accepted) == 3);
    close_fixture();
    make_metadata(uri, 0, true); wire = metadata;
    make_metadata("spotify:track:0000000000000000000001", 1, false);
    const MediaWire *alternatives[] = {&wire, &metadata, &storage}; open_fixture(alternatives, 3, false);
    CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &id) == MEDIA_OK && drive(id, &source) == MEDIA_OK);
    CHECK(strcmp(source.original_uri, uri) == 0 && source.track.gid[15] == 1 && fixture.key_request[35] == 1);
    close_fixture();
}

static void cancel_deadline_and_capacity(void)
{
    make_metadata(uri, 0, false); const MediaWire *messages[] = {&metadata}; open_fixture(messages, 1, true);
    MediaId ids[MEDIA_SOURCE_SLOTS], extra;
    for (size_t i = 0; i < MEDIA_SOURCE_SLOTS; ++i) { CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &ids[i]) == MEDIA_OK); }
    CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &extra) == MEDIA_FULL);
    CHECK(media_source_poll(fixture.media) == MEDIA_OK);
    CHECK(media_source_retire(fixture.media, ids[0]) == MEDIA_OK && fixture.callbacks == 1);
    for (size_t i = 1; i < MEDIA_SOURCE_SLOTS; ++i) { CHECK(media_source_retire(fixture.media, ids[i]) == MEDIA_OK); }
    CHECK(fixture.callbacks == MEDIA_SOURCE_SLOTS && fixture.key_sends == 0);
    close_fixture();
    open_fixture(messages, 1, true); CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &extra) == MEDIA_OK);
    fixture.now += 15000; CHECK(media_source_poll(fixture.media) == MEDIA_OK);
    CHECK(fixture.callbacks == 1 && fixture.result == MEDIA_TIMEOUT && fixture.key_sends == 0);
    close_fixture();
    open_fixture(messages, 1, false); CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &extra) == MEDIA_OK);
    uint64_t until = GetTickCount64() + 5000;
    while (fixture.key_sends == 0) {
        CHECK(GetTickCount64() < until && media_source_poll(fixture.media) == MEDIA_OK);
        if (fixture.media->active_slot >= 0) { CHECK(http_wait(fixture.http, 1000) == HTTP_OK); }
    }
    CHECK(media_source_next_wait_ms(fixture.media) == 1500);
    fixture.now += 1500; CHECK(media_source_poll(fixture.media) == MEDIA_OK);
    CHECK(fixture.callbacks == 1 && fixture.result == MEDIA_TIMEOUT && audio_key_stats(fixture.keys).pending == 0);
    close_fixture();
}

static void refresh_preserves_valid_urls(void)
{
    make_metadata(uri, 0, false); make_storage(); wire = storage;
    media_text(&wire, 2, "https://cdn.example/audio?verify=2000000007-short");
    const MediaWire *messages[] = {&metadata, &wire, &storage}; open_fixture(messages, 3, false);
    MediaId id; MediaSource source;
    CHECK(media_source_resolve(fixture.media, uri, completed, NULL, &id) == MEDIA_OK && drive(id, &source) == MEDIA_OK);
    CHECK(source.url_count == 2); fixture.now += 2000;
    CHECK(media_source_poll(fixture.media) == MEDIA_OK && fixture.media->slots[id.slot].stage == MS_STORAGE);
    CHECK(media_source_get(fixture.media, id, &source) == MEDIA_OK && source.url_count == 1);
    MediaKeyLease lease = {0}; CHECK(media_key_acquire(fixture.media, source.key, &lease) == MEDIA_OK && lease.key[0] == 0xa5);
    CHECK(media_key_release(fixture.media, &lease) == MEDIA_OK);
    uint64_t until = GetTickCount64() + 5000;
    while (fixture.media->slots[id.slot].stage == MS_STORAGE) {
        CHECK(GetTickCount64() < until && http_wait(fixture.http, 1000) == HTTP_OK && media_source_poll(fixture.media) == MEDIA_OK);
    }
    CHECK(media_source_get(fixture.media, id, &source) == MEDIA_OK && fixture.callbacks == 1 && fixture.key_sends == 1);
    close_fixture();
}

int main(void)
{
    ids(); metadata_tests(); storage_tests(); successful_source_and_leases(); refresh_and_alternatives();
    cancel_deadline_and_capacity(); refresh_preserves_valid_urls();
    puts("Media source: bounded metadata, storage, AudioKey leases and cleanup passed"); return 0;
}
