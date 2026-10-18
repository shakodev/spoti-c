#include "mercury_test.h"
#include "protocol/ap_internal.h"
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

MercuryFixture mf;
const uint8_t mercury_header[11] = {0x0a,6,'h','m',':','/','/','x',0x20,0x90,3};
static uint8_t frame[65535], wire[AP_WIRE_MAX];

static uint64_t now_ms(void *context) { return ((MercuryFixture *)context)->now; }
static ApResult send_packet(void *context, uint8_t command, const void *data, size_t bytes)
{
    MercuryFixture *f = context;
    CHECK(bytes <= sizeof(f->sent));
    if (f->send_result != AP_OK) { return f->send_result; }
    ++f->sends; f->sent_command = command; f->sent_bytes = bytes; memcpy(f->sent, data, bytes);
    return AP_OK;
}

void mercury_record(void *context, MercuryId id, ApResult result, const MercuryResponse *response)
{
    CHECK(context == &mf && mf.records < 80);
    MercuryRecord *r = &mf.record[mf.records++]; memset(r, 0, sizeof(*r));
    r->id = id; r->result = result; r->response = response != NULL;
    if (response != NULL) {
        r->event = response->event; r->status = response->status; r->count = response->count;
        CHECK(strlen(response->uri) < sizeof(r->uri)); strcpy_s(r->uri, sizeof(r->uri), response->uri);
        for (size_t i = 0; i < response->count; ++i) {
            MercurySpan part = response->parts[i];
            CHECK(part.bytes <= sizeof(r->data) - r->copied);
            r->lengths[i] = part.bytes;
            if (part.bytes != 0) { memcpy(r->data + r->copied, part.data, part.bytes); }
            r->copied += part.bytes;
        }
    }
    if (mf.reenter) {
        CHECK(mercury_cancel(mf.client, id) == AP_BAD_STATE);
        CHECK(mercury_tick(mf.client) == AP_BAD_STATE);
        CHECK(mercury_disconnect(mf.client, false) == AP_BAD_STATE);
        MercuryId next;
        CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, NULL, 0,
            CORE_CLOCK_NEVER, mercury_record, &mf, &next) == AP_BAD_STATE);
        if (response != NULL && r->copied != 0) { CHECK(memcmp(response->parts[0].data, r->data, r->lengths[0]) == 0); }
    }
}

void mercury_fixture_open(bool resubscribe)
{
    memset(&mf, 0, sizeof(mf)); mf.now = 100; mf.generation = 1;
    CHECK(metrics_init(&mf.metrics)); CHECK(arena_init(&mf.arena, mf.storage, sizeof(mf.storage)));
    CHECK(metrics_attach_arena(&mf.metrics, &mf.arena, &mf.binding));
    MercuryConfig config = {send_packet, &mf, {now_ms, &mf}, resubscribe, 1000};
    CHECK(mercury_create(&mf.arena, &config, &mf.client) == AP_OK);
    mf.arena_used = mf.arena.used;
    CHECK(mercury_connect(mf.client, mf.generation) == AP_OK);
    uint8_t key[32] = {0};
    CHECK(ap_packet_init(&mf.local, key, key) == AP_OK);
    CHECK(ap_packet_init(&mf.remote, key, key) == AP_OK);
#if defined(_MSC_VER) && defined(_DEBUG)
    heap_allocations = 0; previous_hook = _CrtSetAllocHook(allocation_hook);
#endif
}

void mercury_fixture_close(void)
{
    CHECK(mf.arena.used == mf.arena_used);
    CHECK(mercury_disconnect(mf.client, false) == AP_OK);
    MercuryStats stats = mercury_stats(mf.client);
    CHECK(stats.pool_used == 0 && stats.pending == 0 && stats.events == 0 && stats.subscriptions == 0);
    CHECK(stats.pool_high_water <= MERCURY_POOL);
    ap_packet_clear(&mf.local); ap_packet_clear(&mf.remote);
    arena_dispose(&mf.arena); CHECK(metrics_balanced(&mf.metrics));
#if defined(_MSC_VER) && defined(_DEBUG)
    (void)_CrtSetAllocHook(previous_hook); CHECK(heap_allocations == 0);
#endif
}

MercuryId mercury_get(uint64_t deadline)
{
    MercuryId id;
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, NULL, 0,
        deadline, mercury_record, &mf, &id) == AP_OK); return id;
}

size_t mercury_frame(uint8_t *out, uint64_t seq, uint8_t flags, const MercurySpan *parts, size_t count)
{
    out[0] = 0; out[1] = 8;
    for (unsigned i = 0; i < 8; ++i) { out[2 + i] = (uint8_t)(seq >> ((7 - i) * 8)); }
    out[10] = flags; out[11] = (uint8_t)(count >> 8); out[12] = (uint8_t)count;
    size_t pos = 13;
    for (size_t i = 0; i < count; ++i) {
        CHECK(parts[i].bytes <= UINT16_MAX && pos + 2 + parts[i].bytes <= 65535);
        out[pos++] = (uint8_t)(parts[i].bytes >> 8); out[pos++] = (uint8_t)parts[i].bytes;
        if (parts[i].bytes != 0) { memcpy(out + pos, parts[i].data, parts[i].bytes); }
        pos += parts[i].bytes;
    }
    return pos;
}

ApResult mercury_deliver(uint8_t command, const void *data, size_t bytes)
{
    CHECK(ap_packet_send(&mf.remote, command, data, bytes) == AP_OK);
    size_t consumed;
    CHECK(ap_packet_feed(&mf.local, mf.remote.tx, mf.remote.tx_bytes, &consumed) == AP_OK);
    CHECK(consumed == mf.remote.tx_bytes);
    CHECK(ap_packet_advance(&mf.remote, consumed) == AP_OK);
    ApResult result = mercury_on_packet(mf.client, mf.generation, &mf.local);
    CHECK(ap_packet_consume(&mf.local) == AP_OK); return result;
}

ApResult mercury_reply(uint8_t command, uint64_t seq, uint8_t flags, const MercurySpan *parts, size_t count)
{
    size_t bytes = mercury_frame(frame, seq, flags, parts, count);
    return mercury_deliver(command, frame, bytes);
}

ApResult mercury_ok(uint8_t command, MercuryId id)
{
    MercurySpan part = {mercury_header, sizeof(mercury_header)};
    return mercury_reply(command, id.value, 1, &part, 1);
}

static void golden(void)
{
    mercury_fixture_open(false);
    static const uint8_t get[] = {0,8,0,0,0,0,0,0,0,1,1,0,1,0,13,
        0x0a,6,'h','m',':','/','/','x',0x1a,3,'G','E','T'};
    MercuryId first = mercury_get(CORE_CLOCK_NEVER);
    CHECK(first.value == 1 && mf.sent_command == MERCURY_REQ && mf.sent_bytes == sizeof(get));
    CHECK(memcmp(mf.sent, get, sizeof(get)) == 0);
    static const uint8_t response[] = {0,8,0,0,0,0,0,0,0,1,1,0,3,0,11,
        0x0a,6,'h','m',':','/','/','x',0x20,0x90,3,0,3,'a','b','c',0,0};
    mf.reenter = true;
    CHECK(mercury_deliver(MERCURY_REQ, response, sizeof(response)) == AP_OK);
    CHECK(mf.records == 1 && mf.record[0].status == 200 && mf.record[0].count == 2);
    CHECK(mf.record[0].lengths[0] == 3 && mf.record[0].lengths[1] == 0);
    CHECK(memcmp(mf.record[0].data, "abc", 3) == 0);
    CHECK(mercury_deliver(MERCURY_REQ, response, sizeof(response)) == AP_STALE && mf.records == 1);
    mf.reenter = false;
    MercuryId second; MercurySpan body = {(const uint8_t *)"abc", 3};
    CHECK(mercury_request(mf.client, MERCURY_SEND, "hm://x", "x", &body, 1,
        500, mercury_record, &mf, &second) == AP_OK);
    static const uint8_t send[] = {0,8,0,0,0,0,0,0,0,2,1,0,2,0,17,
        0x0a,6,'h','m',':','/','/','x',0x12,1,'x',0x1a,4,'S','E','N','D',0,3,'a','b','c'};
    CHECK(mf.sent_bytes == sizeof(send) && memcmp(mf.sent, send, sizeof(send)) == 0);
    CHECK(mercury_ok(MERCURY_SUB, second) == AP_STALE && mf.records == 1);
    static const uint8_t error[] = {0x20,0xa6,6}; /* sint32 403 */
    MercurySpan e = {error, sizeof(error)};
    CHECK(mercury_reply(MERCURY_REQ, second.value, 1, &e, 1) == AP_IO_ERROR);
    CHECK(mf.records == 2 && mf.record[1].status == 403 && strcmp(mf.record[1].uri, "hm://x") == 0);
    mercury_fixture_close();
}

static void split_and_out_of_order(void)
{
    mercury_fixture_open(false);
    MercuryId a = mercury_get(500), b = mercury_get(500);
    MercurySpan head = {mercury_header, 5}, rest[] = {{mercury_header + 5, 6}, {(const uint8_t *)"ab", 2}};
    CHECK(mercury_reply(MERCURY_REQ, a.value, 2, &head, 1) == AP_PENDING);
    CHECK(mercury_ok(MERCURY_REQ, b) == AP_OK && mf.record[0].id.value == b.value);
    CHECK(mercury_reply(MERCURY_REQ, a.value, 2, rest, 2) == AP_PENDING);
    MercurySpan end[] = {{(const uint8_t *)"cd", 2}, {NULL, 0}};
    CHECK(mercury_reply(MERCURY_REQ, a.value, 0, end, 2) == AP_PENDING);
    CHECK(mf.records == 1);
    CHECK(mercury_reply(MERCURY_REQ, a.value, 1, NULL, 0) == AP_OK);
    CHECK(mf.records == 2 && mf.record[1].count == 2 && mf.record[1].lengths[0] == 4);
    CHECK(memcmp(mf.record[1].data, "abcd", 4) == 0 && mercury_stats(mf.client).pool_used == 0);
    CHECK(mercury_cancel(mf.client, a) == AP_STALE);
    mercury_fixture_close();
}

static void authentication_boundary(void)
{
    mercury_fixture_open(false); MercuryId id = mercury_get(500);
    MercurySpan head = {mercury_header, sizeof(mercury_header)};
    size_t bytes = mercury_frame(frame, id.value, 1, &head, 1);
    CHECK(ap_packet_send(&mf.remote, MERCURY_REQ, frame, bytes) == AP_OK);
    bytes = mf.remote.tx_bytes; memcpy(wire, mf.remote.tx, bytes);
    for (size_t i = 0; i < bytes; ++i) {
        size_t used; CHECK(ap_packet_feed(&mf.local, wire + i, 1, &used) == (i + 1 == bytes ? AP_OK : AP_PENDING));
        CHECK(used == 1);
        if (i + 1 != bytes) {
            CHECK(mercury_on_packet(mf.client, 1, &mf.local) == AP_PENDING && mf.records == 0);
        }
    }
    static ApSession session;
    memset(&session, 0, sizeof(session)); session.generation = 1;
    session.packet = mf.local; session.state = AP_STATE_FRAMED;
    CHECK(ap_dispatch_mercury(&session, mf.client) == AP_BAD_STATE && mf.records == 0);
    session.state = AP_STATE_AUTHENTICATED;
    CHECK(mercury_on_packet(mf.client, 2, &mf.local) == AP_STALE && mf.records == 0);
    CHECK(ap_dispatch_mercury(&session, mf.client) == AP_OK && mf.records == 1);
    ap_packet_clear(&session.packet); mercury_fixture_close();
    mercury_fixture_open(false); (void)mercury_get(500);
    uint8_t key[32] = {0};
    for (size_t pos = 0; pos < bytes; ++pos) {
        if (pos == 1 || pos == 2) { continue; }
        ap_packet_clear(&mf.local); CHECK(ap_packet_init(&mf.local, key, key) == AP_OK);
        wire[pos] ^= 1; size_t used;
        CHECK(ap_packet_feed(&mf.local, wire, bytes, &used) == AP_BAD_MAC);
        CHECK(mercury_on_packet(mf.client, 1, &mf.local) == AP_BAD_MAC && mf.records == 0);
        wire[pos] ^= 1;
    }
    mercury_fixture_close();
}

int main(void)
{
    golden(); split_and_out_of_order(); authentication_boundary();
    mercury_limit_tests(); mercury_subscription_tests();
    puts("Mercury: golden framing, bounded multipart, lifecycle and verified dispatch passed"); return 0;
}
