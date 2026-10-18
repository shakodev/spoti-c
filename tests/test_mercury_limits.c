#include "mercury_test.h"

static uint8_t bytes[65535], payload[MERCURY_RESPONSE_MAX];
static ApResult discard(void *context, uint8_t command, const void *data, size_t size)
{
    (void)context; (void)command; (void)data; (void)size; return AP_OK;
}

static void admission_and_deadlines(void)
{
    mercury_fixture_open(false); MercuryId ids[MERCURY_REQUESTS], extra = {999};
    for (size_t i = 0; i < MERCURY_REQUESTS; ++i) { ids[i] = mercury_get(500); }
    CHECK(mercury_stats(mf.client).pending == MERCURY_REQUESTS && mercury_next_wait_ms(mf.client) == 400);
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, NULL, 0,
        500, mercury_record, &mf, &extra) == AP_FULL && extra.value == 999);
    CHECK(mercury_cancel(mf.client, ids[3]) == AP_OK && mf.records == 1 && mf.record[0].result == AP_CANCELLED);
    extra = mercury_get(700); CHECK(extra.value > ids[15].value);
    CHECK(mercury_cancel(mf.client, ids[3]) == AP_STALE);
    CHECK(mercury_ok(MERCURY_REQ, ids[3]) == AP_STALE && mf.records == 1);
    mf.now = 500; CHECK(mercury_next_wait_ms(mf.client) == 0);
    CHECK(mercury_tick(mf.client) == AP_OK && mf.records == 16);
    for (size_t i = 1; i < 16; ++i) { CHECK(mf.record[i].result == AP_TIMEOUT); }
    CHECK(mercury_next_wait_ms(mf.client) == 200 && mercury_stats(mf.client).pending == 1);
    CHECK(mercury_ok(MERCURY_REQ, ids[0]) == AP_STALE);
    mf.now = 701;
    CHECK(mercury_ok(MERCURY_REQ, extra) == AP_TIMEOUT && mf.record[16].result == AP_TIMEOUT);
    CHECK(mercury_next_wait_ms(mf.client) == UINT32_MAX);
    size_t sends = mf.sends;
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, NULL, 0,
        mf.now, mercury_record, &mf, &extra) == AP_TIMEOUT && mf.sends == sends);
    mf.send_result = AP_FULL;
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, NULL, 0,
        900, mercury_record, &mf, &extra) == AP_FULL && mercury_stats(mf.client).pending == 0);
    mf.send_result = AP_OK; extra = mercury_get(900);
    CHECK(mercury_cancel(mf.client, extra) == AP_OK);
    mf.client->sequence = UINT64_MAX;
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, NULL, 0,
        900, mercury_record, &mf, &extra) == AP_EXHAUSTED);
    mercury_fixture_close();
}

static void malformed(void)
{
    MercurySpan head = {mercury_header, sizeof(mercury_header)};
    mercury_fixture_open(false);
    size_t valid = mercury_frame(bytes, 1, 1, &head, 1);
    for (size_t n = 0; n < valid; ++n) {
        MercuryId id = mercury_get(500); mercury_frame(bytes, id.value, 1, &head, 1);
        CHECK(mercury_deliver(MERCURY_REQ, bytes, n) == AP_MALFORMED);
        /* Short envelope cannot identify a request; explicit cancel retires it. */
        if (n < 13) { CHECK(mercury_cancel(mf.client, id) == AP_OK); }
        CHECK(mercury_stats(mf.client).pending == 0 && mercury_stats(mf.client).pool_used == 0);
    }
    MercuryId id = mercury_get(500);
    size_t n = mercury_frame(bytes, id.value, 1, &head, 1); bytes[n] = 0;
    CHECK(mercury_deliver(MERCURY_REQ, bytes, n + 1) == AP_MALFORMED);
    id = mercury_get(500); n = mercury_frame(bytes, id.value, 3, &head, 1);
    CHECK(mercury_deliver(MERCURY_REQ, bytes, n) == AP_MALFORMED);
    id = mercury_get(500); n = mercury_frame(bytes, id.value, 0, NULL, 0);
    CHECK(mercury_deliver(MERCURY_REQ, bytes, n) == AP_MALFORMED);
    id = mercury_get(500); n = mercury_frame(bytes, id.value, 1, &head, 1); bytes[13] = bytes[14] = 255;
    CHECK(mercury_deliver(MERCURY_REQ, bytes, n) == AP_MALFORMED);
    id = mercury_get(500); MercurySpan empty = {NULL, 0};
    CHECK(mercury_reply(MERCURY_REQ, id.value, 1, &empty, 1) == AP_MALFORMED);
    id = mercury_get(500); uint8_t bad_proto[] = {0x0a,255}; MercurySpan bad = {bad_proto, 2};
    CHECK(mercury_reply(MERCURY_REQ, id.value, 1, &bad, 1) == AP_MALFORMED);
    id = mercury_get(500); n = mercury_frame(bytes, id.value, 1, &head, 1); bytes[1] = 0;
    CHECK(mercury_deliver(MERCURY_REQ, bytes, n) == AP_MALFORMED);
    CHECK(mercury_cancel(mf.client, id) == AP_OK);
    id = mercury_get(500); n = mercury_frame(bytes, id.value, 1, &head, 1); bytes[12] = MERCURY_PARTS + 1;
    CHECK(mercury_deliver(MERCURY_REQ, bytes, n) == AP_LIMIT);
    CHECK(mercury_deliver(0x0d, bytes, n) == AP_UNSUPPORTED); /* AudioKey stays distinct. */
    id = mercury_get(500);
    CHECK(mercury_reply(MERCURY_REQ, id.value, 2, &head, 1) == AP_PENDING);
    CHECK(mercury_reply(MERCURY_REQ, id.value, 1, NULL, 0) == AP_MALFORMED);
    mercury_fixture_close();
}

static void pool_and_response_limits(void)
{
    mercury_fixture_open(false); memset(payload, 0x5a, sizeof(payload));
    MercuryId ids[5];
    MercurySpan parts[] = {{mercury_header, sizeof(mercury_header)},
        {payload, MERCURY_RESPONSE_MAX - sizeof(mercury_header)}};
    for (size_t i = 0; i < 4; ++i) {
        ids[i] = mercury_get(500);
        CHECK(mercury_reply(MERCURY_REQ, ids[i].value, 0, parts, 2) == AP_PENDING);
    }
    CHECK(mercury_stats(mf.client).pool_used == MERCURY_POOL);
    CHECK(mercury_stats(mf.client).pool_high_water == MERCURY_POOL);
    ids[4] = mercury_get(500);
    CHECK(mercury_ok(MERCURY_REQ, ids[4]) == AP_FULL && mf.record[0].result == AP_FULL);
    CHECK(mercury_cancel(mf.client, ids[1]) == AP_OK);
    CHECK(mercury_reply(MERCURY_REQ, ids[3].value, 1, NULL, 0) == AP_OK);
    CHECK(mf.record[2].copied == parts[1].bytes);
    for (size_t i = 0; i < mf.record[2].copied; ++i) { CHECK(mf.record[2].data[i] == 0x5a); }
    CHECK(mercury_reply(MERCURY_REQ, ids[0].value, 1, NULL, 0) == AP_OK);
    CHECK(mercury_reply(MERCURY_REQ, ids[2].value, 1, NULL, 0) == AP_OK);
    CHECK(mercury_stats(mf.client).pool_used == 0);
    MercuryId id = mercury_get(500); parts[1].bytes = MERCURY_RESPONSE_MAX;
    CHECK(mercury_reply(MERCURY_REQ, id.value, 1, parts, 2) == AP_LIMIT);
    /* Extend a middle allocation; compaction must preserve all other responses. */
    id = mercury_get(500); MercuryId b = mercury_get(500);
    CHECK(mercury_reply(MERCURY_REQ, id.value, 0, parts, 1) == AP_PENDING);
    CHECK(mercury_reply(MERCURY_REQ, b.value, 0, parts, 1) == AP_PENDING);
    MercurySpan body = {(const uint8_t *)"middle", 6};
    CHECK(mercury_reply(MERCURY_REQ, id.value, 1, &body, 1) == AP_OK);
    CHECK(mercury_reply(MERCURY_REQ, b.value, 1, &body, 1) == AP_OK);
    CHECK(memcmp(mf.record[mf.records - 1].data, "middle", 6) == 0);
    mercury_fixture_close();
}

static void parts_fragments_and_arguments(void)
{
    mercury_fixture_open(false); MercuryId id = mercury_get(500);
    MercurySpan parts[MERCURY_PARTS] = {{mercury_header, sizeof(mercury_header)}};
    CHECK(mercury_reply(MERCURY_REQ, id.value, 0, parts, MERCURY_PARTS) == AP_PENDING);
    MercurySpan empty = {NULL, 0};
    CHECK(mercury_reply(MERCURY_REQ, id.value, 1, &empty, 1) == AP_LIMIT);
    id = mercury_get(500);
    CHECK(mercury_reply(MERCURY_REQ, id.value, 2, parts, 1) == AP_PENDING);
    for (size_t i = 1; i < MERCURY_FRAGMENTS; ++i) {
        CHECK(mercury_reply(MERCURY_REQ, id.value, 2, &empty, 1) == AP_PENDING);
    }
    CHECK(mercury_reply(MERCURY_REQ, id.value, 1, &empty, 1) == AP_LIMIT);
    size_t sends = mf.sends; MercuryId out = {123};
    CHECK(mercury_request(mf.client, MERCURY_GET, "", NULL, NULL, 0, 500, mercury_record, &mf, &out) == AP_INVALID);
    memset(bytes, 'a', 257); bytes[257] = 0;
    CHECK(mercury_request(mf.client, MERCURY_GET, (char *)bytes, NULL, NULL, 0,
        500, mercury_record, &mf, &out) == AP_INVALID);
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", "\xff", NULL, 0,
        500, mercury_record, &mf, &out) == AP_INVALID);
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, parts, MERCURY_PARTS,
        500, mercury_record, &mf, &out) == AP_LIMIT);
    MercurySpan huge = {payload, SIZE_MAX};
    CHECK(mercury_request(mf.client, MERCURY_SEND, "hm://x", NULL, &huge, 1,
        500, mercury_record, &mf, &out) == AP_LIMIT);
    huge = (MercurySpan){NULL, 1};
    CHECK(mercury_request(mf.client, MERCURY_SEND, "hm://x", NULL, &huge, 1,
        500, mercury_record, &mf, &out) == AP_INVALID);
    CHECK(mercury_request(mf.client, MERCURY_GET, "hm://x", NULL, NULL, 0,
        500, NULL, &mf, &out) == AP_INVALID);
    CHECK(mf.sends == sends && out.value == 123);
    CHECK(mercury_connect(mf.client, 1) == AP_BAD_STATE);
    CHECK(mercury_cancel(mf.client, (MercuryId){0}) == AP_INVALID);
    mercury_fixture_close();
    /* Failed arena admission is transactional, without heap fallback. */
    Arena a; uint8_t small[16]; MercuryClient *c = NULL;
    MercuryConfig config = {0};
    CHECK(arena_init(&a, small, sizeof(small)));
    CHECK(mercury_create(&a, &config, &c) == AP_INVALID && a.used == 0 && c == NULL);
    config.send = discard;
    CHECK(mercury_create(&a, &config, &c) == AP_NO_MEMORY && a.used == 0 && c == NULL);
    arena_dispose(&a);
}

static void unsolicited_and_event_capacity(void)
{
    mercury_fixture_open(false); MercurySpan head = {mercury_header, sizeof(mercury_header)};
    CHECK(mercury_ok(MERCURY_EVENT, (MercuryId){0}) == AP_STALE && mf.records == 0);
    CHECK(mercury_ok(MERCURY_EVENT, (MercuryId){0}) == AP_STALE);
    for (uint64_t i = 1; i <= MERCURY_EVENTS; ++i) {
        CHECK(mercury_reply(MERCURY_EVENT, i, 0, &head, 1) == AP_PENDING);
    }
    CHECK(mercury_reply(MERCURY_EVENT, 9, 0, &head, 1) == AP_FULL);
    CHECK(mercury_stats(mf.client).events == MERCURY_EVENTS && mf.records == 0);
    CHECK(mercury_next_wait_ms(mf.client) == 1000);
    mf.now += 1000; CHECK(mercury_tick(mf.client) == AP_OK);
    CHECK(mercury_stats(mf.client).events == 0 && mercury_stats(mf.client).pool_used == 0);
    CHECK(mercury_reply(MERCURY_EVENT, 1, 1, &head, 1) == AP_STALE && mf.records == 0);
    mercury_fixture_close();
}

void mercury_limit_tests(void)
{
    admission_and_deadlines(); malformed(); pool_and_response_limits();
    parts_fragments_and_arguments(); unsolicited_and_event_capacity();
}
