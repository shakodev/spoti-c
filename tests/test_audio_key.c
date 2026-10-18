#include "protocol/audio_key_internal.h"
#include "core/metrics.h"
#include "support/test_check.h"
#include <string.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static _CRT_ALLOC_HOOK previous_hook;
static size_t heap_allocations;
static int allocation_hook(int type, void *data, size_t bytes, int block,
    long request, const unsigned char *file, int line)
{
    if (type == _HOOK_ALLOC && block != _CRT_BLOCK) { ++heap_allocations; }
    return previous_hook == NULL ? 1 : previous_hook(type, data, bytes, block, request, file, line);
}
#endif

typedef struct Record { AudioKeyId id; ApResult result; bool key; uint8_t data[16]; } Record;
static struct {
    _Alignas(16) uint8_t storage[4096];
    Arena arena;
    Metrics metrics;
    MetricsArena binding;
    AudioKeyClient *client;
    ApPacket local, remote;
    uint64_t now;
    uint32_t generation;
    ApResult send_result;
    size_t sends, sent_bytes, records, arena_used;
    uint8_t command, sent[42];
    Record record[32];
    bool reenter, send_reenter;
} f;
static const uint8_t gid[16] = {0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
    0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};
static const uint8_t file_id[20] = {0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,
    0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,0x30,0x31,0x32,0x33};
static uint8_t wire[AP_WIRE_MAX];
static void record(void *, AudioKeyId, ApResult, const uint8_t *);

static void reentry(AudioKeyId id)
{
    AudioKeyId next;
    CHECK(audio_key_cancel(f.client, id) == AP_BAD_STATE);
    CHECK(audio_key_tick(f.client) == AP_BAD_STATE);
    CHECK(audio_key_disconnect(f.client, false) == AP_BAD_STATE);
    CHECK(audio_key_connect(f.client, f.generation + 1) == AP_BAD_STATE);
    CHECK(audio_key_request(f.client, gid, file_id, 500, record, &f, &next) == AP_BAD_STATE);
    CHECK(audio_key_on_packet(f.client, f.generation, &f.local) == AP_BAD_STATE);
}

static uint64_t now_ms(void *context) { CHECK(context == &f); return f.now; }
static ApResult send_packet(void *context, uint8_t command, const void *data, size_t bytes)
{
    CHECK(context == &f && bytes == sizeof(f.sent));
    if (f.send_reenter) { reentry((AudioKeyId){1}); }
    if (f.send_result != AP_OK) { return f.send_result; }
    ++f.sends; f.command = command; f.sent_bytes = bytes; memcpy(f.sent, data, bytes); return AP_OK;
}

static void record(void *context, AudioKeyId id, ApResult result, const uint8_t *key)
{
    CHECK(context == &f && f.records < 32);
    Record *r = &f.record[f.records++];
    *r = (Record){0}; r->id = id; r->result = result; r->key = key != NULL;
    CHECK((result == AP_OK) == (key != NULL));
    if (key != NULL) { memcpy(r->data, key, 16); }
    if (f.reenter) {
        reentry(id);
        if (key != NULL) { CHECK(memcmp(key, r->data, 16) == 0); }
    }
}

static void open_fixture(uint32_t timeout_ms)
{
    memset(&f, 0, sizeof(f)); f.now = 100; f.generation = 1;
    CHECK(metrics_init(&f.metrics)); CHECK(arena_init(&f.arena, f.storage, sizeof(f.storage)));
    CHECK(metrics_attach_arena(&f.metrics, &f.arena, &f.binding));
    AudioKeyConfig config = {send_packet, &f, {now_ms, &f}, timeout_ms};
    CHECK(audio_key_create(&f.arena, &config, &f.client) == AP_OK);
    f.arena_used = f.arena.used;
    CHECK(audio_key_connect(f.client, f.generation) == AP_OK);
    uint8_t key[32] = {0};
    CHECK(ap_packet_init(&f.local, key, key) == AP_OK);
    CHECK(ap_packet_init(&f.remote, key, key) == AP_OK);
#if defined(_MSC_VER) && defined(_DEBUG)
    heap_allocations = 0; previous_hook = _CrtSetAllocHook(allocation_hook);
#endif
}

static void close_fixture(void)
{
    CHECK(f.arena.used == f.arena_used);
    CHECK(audio_key_disconnect(f.client, false) == AP_OK);
    AudioKeyStats stats = audio_key_stats(f.client);
    CHECK(stats.pending == 0 && stats.pending_high_water <= AUDIO_KEY_REQUESTS);
    MetricsSnapshot snap = metrics_snapshot(&f.metrics);
    CHECK(snap.owned_allocations == 0 && snap.handles == 0 && snap.arena_used == f.arena_used);
    ap_packet_clear(&f.local); ap_packet_clear(&f.remote);
    arena_dispose(&f.arena); CHECK(metrics_balanced(&f.metrics));
#if defined(_MSC_VER) && defined(_DEBUG)
    (void)_CrtSetAllocHook(previous_hook); CHECK(heap_allocations == 0);
#endif
}

static AudioKeyId request(uint64_t deadline)
{
    AudioKeyId id;
    CHECK(audio_key_request(f.client, gid, file_id, deadline, record, &f, &id) == AP_OK); return id;
}

static void body(uint8_t out[24], AudioKeyId id)
{
    for (size_t i = 0; i < 24; ++i) { out[i] = (uint8_t)(0xa0 + i); }
    out[0] = (uint8_t)(id.value >> 24); out[1] = (uint8_t)(id.value >> 16);
    out[2] = (uint8_t)(id.value >> 8); out[3] = (uint8_t)id.value;
}

static ApResult deliver(uint8_t command, const uint8_t *data, size_t bytes)
{
    CHECK(ap_packet_send(&f.remote, command, data, bytes) == AP_OK);
    size_t used;
    CHECK(ap_packet_feed(&f.local, f.remote.tx, f.remote.tx_bytes, &used) == AP_OK);
    CHECK(used == f.remote.tx_bytes); CHECK(ap_packet_advance(&f.remote, used) == AP_OK);
    ApResult result = audio_key_on_packet(f.client, f.generation, &f.local);
    CHECK(ap_packet_consume(&f.local) == AP_OK); return result;
}

static ApResult reply(uint8_t command, AudioKeyId id, size_t bytes)
{
    uint8_t data[24]; body(data, id); CHECK(bytes <= sizeof(data)); return deliver(command, data, bytes);
}

static void golden(void)
{
    open_fixture(0); f.send_reenter = true;
    AudioKeyId id = request(CORE_CLOCK_NEVER);
    static const uint8_t expected[42] = {
        0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,0x2c,0x2d,0x2e,0x2f,
        0x30,0x31,0x32,0x33,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,
        0x1c,0x1d,0x1e,0x1f,0,0,0,1,0,0};
    CHECK(id.value == 1 && f.command == 0x0c && f.sent_bytes == sizeof(expected));
    CHECK(memcmp(f.sent, expected, sizeof(expected)) == 0);
    CHECK(audio_key_next_wait_ms(f.client) == 1500);
    f.reenter = true;
    CHECK(reply(0x0d, id, 20) == AP_OK && f.records == 1);
    CHECK(f.record[0].id.value == id.value && f.record[0].result == AP_OK && f.record[0].key);
    for (size_t i = 0; i < 16; ++i) { CHECK(f.record[0].data[i] == (uint8_t)(0xa4 + i)); }
    CHECK(reply(0x0d, id, 20) == AP_STALE && f.records == 1);
    CHECK(audio_key_next_wait_ms(f.client) == UINT32_MAX);
    id = request(500);
    CHECK(reply(0x0e, id, 6) == AP_IO_ERROR && f.records == 2);
    CHECK(f.record[1].result == AP_IO_ERROR && !f.record[1].key);
    close_fixture();
}

static void malformed(void)
{
    open_fixture(1000);
    for (size_t bytes = 0; bytes <= 23; ++bytes) {
        if (bytes == 20) { continue; }
        AudioKeyId id = request(500); size_t before = f.records;
        CHECK(reply(AUDIO_KEY_RESPONSE, id, bytes) == AP_MALFORMED);
        if (bytes < 4) {
            CHECK(f.records == before && audio_key_stats(f.client).pending == 1);
            CHECK(audio_key_cancel(f.client, id) == AP_OK);
        } else { CHECK(f.records == before + 1 && f.record[before].result == AP_MALFORMED); }
        CHECK(reply(AUDIO_KEY_RESPONSE, id, 20) == AP_STALE);
        f.records = 0;
    }
    for (size_t bytes = 0; bytes <= 8; ++bytes) {
        if (bytes == 6) { continue; }
        AudioKeyId id = request(500);
        CHECK(reply(AUDIO_KEY_ERROR, id, bytes) == AP_MALFORMED);
        if (bytes < 4) { CHECK(f.records == 0); CHECK(audio_key_cancel(f.client, id) == AP_OK); }
        else { CHECK(f.records == 1 && f.record[0].result == AP_MALFORMED); }
        f.records = 0;
    }
    AudioKeyId id = request(500);
    CHECK(reply(0x0c, id, 20) == AP_UNSUPPORTED && f.records == 0);
    CHECK(reply(AUDIO_KEY_RESPONSE, (AudioKeyId){0}, 20) == AP_STALE && f.records == 0);
    CHECK(reply(AUDIO_KEY_RESPONSE, id, 20) == AP_OK && f.records == 1);
    close_fixture();
}

static void authentication(void)
{
    open_fixture(1000); AudioKeyId id = request(500);
    uint8_t data[24]; body(data, id);
    CHECK(ap_packet_send(&f.remote, AUDIO_KEY_RESPONSE, data, 20) == AP_OK);
    size_t bytes = f.remote.tx_bytes; memcpy(wire, f.remote.tx, bytes);
    for (size_t i = 0; i < bytes; ++i) {
        size_t used;
        CHECK(ap_packet_feed(&f.local, wire + i, 1, &used) == (i + 1 == bytes ? AP_OK : AP_PENDING));
        CHECK(used == 1);
        if (i + 1 != bytes) { CHECK(audio_key_on_packet(f.client, 1, &f.local) == AP_PENDING && f.records == 0); }
    }
    CHECK(audio_key_on_packet(f.client, 2, &f.local) == AP_STALE && f.records == 0);
    CHECK(audio_key_on_packet(f.client, 1, &f.local) == AP_OK && f.records == 1);
    close_fixture();
    open_fixture(1000); (void)request(500);
    uint8_t key[32] = {0};
    for (size_t pos = 0; pos < bytes; ++pos) {
        if (pos == 1 || pos == 2) { continue; } /* Preserve the declared ciphertext length. */
        ap_packet_clear(&f.local); CHECK(ap_packet_init(&f.local, key, key) == AP_OK);
        wire[pos] ^= 1; size_t used;
        CHECK(ap_packet_feed(&f.local, wire, bytes, &used) == AP_BAD_MAC);
        CHECK(audio_key_on_packet(f.client, 1, &f.local) == AP_BAD_MAC && f.records == 0);
        wire[pos] ^= 1;
    }
    close_fixture();
}

static void lifecycle(void)
{
    open_fixture(1000); AudioKeyId ids[AUDIO_KEY_REQUESTS], extra;
    for (size_t i = 0; i < AUDIO_KEY_REQUESTS; ++i) { ids[i] = request(500 + i * 10); }
    CHECK(audio_key_stats(f.client).pending == AUDIO_KEY_REQUESTS);
    CHECK(audio_key_request(f.client, gid, file_id, 600, record, &f, &extra) == AP_FULL && f.sends == 8);
    CHECK(reply(AUDIO_KEY_RESPONSE, ids[7], 20) == AP_OK && f.record[0].id.value == ids[7].value);
    CHECK(audio_key_cancel(f.client, ids[3]) == AP_OK && f.record[1].result == AP_CANCELLED);
    CHECK(audio_key_cancel(f.client, ids[3]) == AP_STALE);
    CHECK(reply(AUDIO_KEY_RESPONSE, ids[3], 20) == AP_STALE && f.records == 2);
    CHECK(audio_key_next_wait_ms(f.client) == 400); f.now = 500;
    CHECK(audio_key_next_wait_ms(f.client) == 0 && audio_key_tick(f.client) == AP_OK);
    CHECK(f.records == 3 && f.record[2].id.value == ids[0].value && f.record[2].result == AP_TIMEOUT);
    f.now = 510;
    CHECK(reply(AUDIO_KEY_RESPONSE, ids[1], 20) == AP_TIMEOUT && f.record[3].result == AP_TIMEOUT);
    f.reenter = true;
    CHECK(audio_key_disconnect(f.client, true) == AP_OK && f.records == 8);
    for (size_t i = 4; i < 8; ++i) { CHECK(f.record[i].result == AP_RECONNECT); }
    CHECK(audio_key_next_wait_ms(f.client) == UINT32_MAX);
    CHECK(audio_key_on_packet(f.client, 1, &f.local) == AP_CLOSED);
    CHECK(audio_key_request(f.client, gid, file_id, 600, record, &f, &extra) == AP_CLOSED);
    CHECK(audio_key_connect(f.client, 1) == AP_OK); /* AP retries retain their generation. */
    CHECK(audio_key_disconnect(f.client, true) == AP_OK);
    f.generation = 2; CHECK(audio_key_connect(f.client, 2) == AP_OK);
    extra = request(600); CHECK(extra.value > ids[7].value && f.sends == 9);
    CHECK(reply(AUDIO_KEY_RESPONSE, ids[2], 20) == AP_STALE && f.records == 8);
    CHECK(audio_key_disconnect(f.client, false) == AP_OK && f.record[8].result == AP_CLOSED);
    CHECK(audio_key_disconnect(f.client, false) == AP_OK && f.records == 9);
    CHECK(audio_key_connect(f.client, 1) == AP_STALE);
    close_fixture();
}

static void admission_and_sequence(void)
{
    open_fixture(1000); AudioKeyId id;
    CHECK(audio_key_request(f.client, NULL, file_id, 500, record, &f, &id) == AP_INVALID);
    CHECK(audio_key_request(f.client, gid, NULL, 500, record, &f, &id) == AP_INVALID);
    CHECK(audio_key_request(f.client, gid, file_id, 500, NULL, &f, &id) == AP_INVALID);
    CHECK(audio_key_request(f.client, gid, file_id, 500, record, &f, NULL) == AP_INVALID);
    CHECK(audio_key_request(f.client, gid, file_id, 100, record, &f, &id) == AP_TIMEOUT && f.sends == 0);
    f.send_result = AP_FULL;
    CHECK(audio_key_request(f.client, gid, file_id, 500, record, &f, &id) == AP_FULL);
    CHECK(audio_key_stats(f.client).pending == 0 && f.records == 0);
    f.send_result = AP_IO_ERROR;
    CHECK(audio_key_request(f.client, gid, file_id, 500, record, &f, &id) == AP_IO_ERROR);
    f.send_result = AP_OK; id = request(500); CHECK(id.value == 3);
    CHECK(reply(AUDIO_KEY_RESPONSE, (AudioKeyId){1}, 20) == AP_STALE);
    CHECK(audio_key_cancel(f.client, id) == AP_OK);
    f.client->sequence = 0x0000fffe;
    id = request(500); CHECK(id.value == 0x0000ffff);
    CHECK(f.sent[36] == 0 && f.sent[37] == 0 && f.sent[38] == 0xff && f.sent[39] == 0xff);
    CHECK(audio_key_cancel(f.client, id) == AP_OK);
    id = request(500); CHECK(id.value == 0x00010000);
    CHECK(f.sent[36] == 0 && f.sent[37] == 1 && f.sent[38] == 0 && f.sent[39] == 0);
    CHECK(audio_key_cancel(f.client, id) == AP_OK);
    f.client->sequence = UINT32_MAX - 1; id = request(500); CHECK(id.value == UINT32_MAX);
    CHECK(reply(AUDIO_KEY_RESPONSE, id, 20) == AP_OK);
    CHECK(audio_key_request(f.client, gid, file_id, 500, record, &f, &id) == AP_EXHAUSTED);
    CHECK(audio_key_disconnect(f.client, true) == AP_OK && audio_key_connect(f.client, 2) == AP_OK);
    CHECK(audio_key_request(f.client, gid, file_id, 500, record, &f, &id) == AP_EXHAUSTED);
    close_fixture();
}

static void cleanup_stress(void)
{
    open_fixture(1000);
    for (size_t i = 0; i < 1000; ++i) {
        AudioKeyId id = request(500);
        if (i % 3 == 0) { CHECK(reply(AUDIO_KEY_RESPONSE, id, 20) == AP_OK); }
        else if (i % 3 == 1) { CHECK(audio_key_cancel(f.client, id) == AP_OK); }
        else { CHECK(reply(AUDIO_KEY_ERROR, id, 6) == AP_IO_ERROR); }
        CHECK(f.records == 1 && audio_key_stats(f.client).pending == 0);
        f.records = 0;
    }
    close_fixture();
    Arena empty; CHECK(arena_init(&empty, NULL, 0));
    AudioKeyConfig config = {send_packet, &f, {now_ms, &f}, 0}; AudioKeyClient *client;
    CHECK(audio_key_create(NULL, &config, &client) == AP_INVALID && client == NULL);
    CHECK(audio_key_create(&empty, NULL, &client) == AP_INVALID && client == NULL);
    CHECK(audio_key_create(&empty, &config, NULL) == AP_INVALID);
    CHECK(audio_key_create(&empty, &config, &client) == AP_NO_MEMORY && client == NULL);
    config.send = NULL;
    CHECK(audio_key_create(&empty, &config, &client) == AP_INVALID && client == NULL);
    arena_dispose(&empty);
}

static void bounded_deadline(void)
{
    open_fixture(0);
    AudioKeyId outer = request(core_deadline_after(f.now, 15000));
    CHECK(audio_key_next_wait_ms(f.client) == 1500);
    f.now = 1599;
    CHECK(audio_key_tick(f.client) == AP_OK && f.records == 0 && audio_key_next_wait_ms(f.client) == 1);
    f.now = 1600;
    CHECK(audio_key_tick(f.client) == AP_OK && f.records == 1);
    CHECK(f.record[0].id.value == outer.value && f.record[0].result == AP_TIMEOUT);
    CHECK(reply(AUDIO_KEY_RESPONSE, outer, 20) == AP_STALE && f.records == 1);
    AudioKeyId tighter = request(f.now + 100);
    CHECK(audio_key_next_wait_ms(f.client) == 100);
    f.now += 100;
    CHECK(reply(AUDIO_KEY_RESPONSE, tighter, 20) == AP_TIMEOUT && f.records == 2);
    close_fixture();
    open_fixture(25);
    AudioKeyId custom = request(CORE_CLOCK_NEVER);
    CHECK(audio_key_next_wait_ms(f.client) == 25);
    f.now += 25; CHECK(audio_key_tick(f.client) == AP_OK && f.record[0].id.value == custom.value);
    close_fixture();
}

int main(void)
{
    golden(); malformed(); authentication(); lifecycle(); admission_and_sequence(); cleanup_stress(); bounded_deadline();
    puts("AudioKey: golden wire, authenticated dispatch, bounded lifecycle and cleanup passed"); return 0;
}
