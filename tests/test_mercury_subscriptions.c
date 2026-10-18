#include "mercury_test.h"

static MercuryId subscribe(const char *uri)
{
    MercuryId id;
    CHECK(mercury_subscribe(mf.client, uri, 500, mercury_record, &mf, &id) == AP_OK); return id;
}

static void subscription_fixtures(void)
{
    mercury_fixture_open(false); MercuryId sub = subscribe("hm://x");
    static const uint8_t request[] = {0,8,0,0,0,0,0,0,0,1,1,0,1,0,13,
        0x0a,6,'h','m',':','/','/','x',0x1a,3,'S','U','B'};
    CHECK(mf.sent_command == MERCURY_SUB && mf.sent_bytes == sizeof(request));
    CHECK(memcmp(mf.sent, request, sizeof(request)) == 0);
    static const uint8_t response[] = {0,8,0,0,0,0,0,0,0,1,1,0,3,0,11,
        0x0a,6,'h','m',':','/','/','x',0x20,0x90,3,
        0,8,0x0a,6,'h','m',':','/','/','a',0,8,0x0a,6,'h','m',':','/','/','b'};
    CHECK(mercury_deliver(MERCURY_SUB, response, sizeof(response)) == AP_OK);
    CHECK(mf.records == 1 && mf.record[0].id.value == sub.value && !mf.record[0].event);
    static const uint8_t event[] = {0,8,0,0,0,0,0,0,0,1,1,0,2,0,13,
        0x0a,8,'h','m',':','/','/','a','/','z',0x20,0x90,3,0,1,'!'};
    CHECK(mercury_deliver(MERCURY_EVENT, event, sizeof(event)) == AP_OK);
    CHECK(mf.records == 2 && mf.record[1].event && mf.record[1].id.value == sub.value);
    CHECK(mf.record[1].count == 1 && mf.record[1].data[0] == '!');
    CHECK(mercury_deliver(MERCURY_EVENT, event, sizeof(event)) == AP_STALE && mf.records == 2);
    static const uint8_t second[] = {0x0a,6,'h','m',':','/','/','b'};
    MercurySpan header = {second, sizeof(second)};
    CHECK(mercury_reply(MERCURY_EVENT, 3, 1, &header, 1) == AP_OK && mf.records == 3);
    MercuryId unsub;
    mf.send_result = AP_FULL;
    CHECK(mercury_unsubscribe(mf.client, sub, 500, mercury_record, &mf, &unsub) == AP_FULL);
    CHECK(mercury_stats(mf.client).subscriptions == 1);
    mf.send_result = AP_OK;
    CHECK(mercury_unsubscribe(mf.client, sub, 500, mercury_record, &mf, &unsub) == AP_OK);
    /* Failed send burned sequence 2; successful UNSUB uses 3. */
    static const uint8_t unsubscribe[] = {0,8,0,0,0,0,0,0,0,3,1,0,1,0,15,
        0x0a,6,'h','m',':','/','/','x',0x1a,5,'U','N','S','U','B'};
    CHECK(mf.sent_command == MERCURY_UNSUB && mf.sent_bytes == sizeof(unsubscribe));
    CHECK(memcmp(mf.sent, unsubscribe, sizeof(unsubscribe)) == 0);
    CHECK(mercury_reply(MERCURY_EVENT, 4, 1, &header, 1) == AP_STALE && mf.records == 3);
    CHECK(mercury_ok(MERCURY_UNSUB, unsub) == AP_OK && mf.records == 4);
    CHECK(mercury_unsubscribe(mf.client, sub, 500, mercury_record, &mf, &unsub) == AP_STALE);
    mercury_fixture_close();
}

static void fallback_and_split_events(void)
{
    mercury_fixture_open(false); MercuryId sub = subscribe("hm://x");
    CHECK(mercury_ok(MERCURY_SUB, sub) == AP_OK); /* Empty payload -> original URI. */
    MercurySpan head = {mercury_header, sizeof(mercury_header)}, body = {(const uint8_t *)"abc", 3};
    CHECK(mercury_reply(MERCURY_EVENT, 100, 2, &head, 1) == AP_PENDING);
    MercurySpan tail[] = {{NULL, 0}, body};
    CHECK(mercury_reply(MERCURY_EVENT, 100, 1, tail, 2) == AP_OK);
    CHECK(mf.records == 2 && mf.record[1].event && mf.record[1].copied == 3);
    CHECK(memcmp(mf.record[1].data, "abc", 3) == 0);
    MercuryId second = subscribe("hm://x"); CHECK(mercury_ok(MERCURY_SUB, second) == AP_OK);
    CHECK(mercury_ok(MERCURY_EVENT, (MercuryId){101}) == AP_OK && mf.records == 5);
    CHECK(mf.record[3].id.value != mf.record[4].id.value);
    CHECK(mercury_cancel(mf.client, sub) == AP_OK);
    CHECK(mercury_cancel(mf.client, second) == AP_OK);
    sub = subscribe("hm://user/%C3%A9"); CHECK(mercury_ok(MERCURY_SUB, sub) == AP_OK);
    static const uint8_t utf8[] = {0x0a,12,'h','m',':','/','/','u','s','e','r','/',0xc3,0xa9};
    MercurySpan encoded = {utf8, sizeof(utf8)};
    CHECK(mercury_reply(MERCURY_EVENT, 102, 1, &encoded, 1) == AP_OK);
    CHECK(mf.record[mf.records - 1].id.value == sub.value);
    mercury_fixture_close();
}

static void subscription_limits(void)
{
    mercury_fixture_open(false); MercuryId ids[MERCURY_SUBSCRIPTIONS], out;
    for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) { ids[i] = subscribe("hm://x"); }
    CHECK(mercury_subscribe(mf.client, "hm://x", 500, mercury_record, &mf, &out) == AP_FULL);
    CHECK(mercury_unsubscribe(mf.client, ids[0], 500, mercury_record, &mf, &out) == AP_BAD_STATE);
    CHECK(mercury_cancel(mf.client, ids[0]) == AP_OK);
    CHECK(mercury_ok(MERCURY_SUB, ids[0]) == AP_STALE);
    MercuryId replacement = subscribe("hm://x");
    CHECK(mercury_ok(MERCURY_SUB, replacement) == AP_OK);
    CHECK(mercury_cancel(mf.client, ids[0]) == AP_STALE);
    CHECK(mercury_stats(mf.client).subscriptions == MERCURY_SUBSCRIPTIONS);
    for (size_t i = 1; i < MERCURY_SUBSCRIPTIONS; ++i) { CHECK(mercury_cancel(mf.client, ids[i]) == AP_OK); }
    CHECK(mercury_cancel(mf.client, replacement) == AP_OK);
    MercuryId sub = subscribe("hm://x");
    MercurySpan parts[MERCURY_ALIASES + 2] = {{mercury_header, sizeof(mercury_header)}};
    static const uint8_t alias[] = {0x0a,6,'h','m',':','/','/','a'};
    for (size_t i = 1; i < MERCURY_ALIASES + 2; ++i) { parts[i] = (MercurySpan){alias, sizeof(alias)}; }
    CHECK(mercury_reply(MERCURY_SUB, sub.value, 1, parts, MERCURY_ALIASES + 2) == AP_LIMIT);
    CHECK(mercury_stats(mf.client).subscriptions == 0);
    sub = subscribe("hm://x"); parts[1] = (MercurySpan){NULL, 0};
    CHECK(mercury_reply(MERCURY_SUB, sub.value, 1, parts, 2) == AP_MALFORMED);
    sub = subscribe("hm://x"); uint8_t invalid[] = {0x0a,255};
    parts[1] = (MercurySpan){invalid, sizeof(invalid)};
    CHECK(mercury_reply(MERCURY_SUB, sub.value, 1, parts, 2) == AP_MALFORMED);
    sub = subscribe("hm://x"); mf.now = 500;
    CHECK(mercury_tick(mf.client) == AP_OK && mercury_stats(mf.client).subscriptions == 0);
    CHECK(mf.record[mf.records - 1].id.value == sub.value && mf.record[mf.records - 1].result == AP_TIMEOUT);
    mercury_fixture_close();
}

static void reconnect(void)
{
    mercury_fixture_open(true); MercuryId sub = subscribe("hm://x");
    CHECK(mercury_ok(MERCURY_SUB, sub) == AP_OK);
    MercuryId request = mercury_get(500);
    MercurySpan head = {mercury_header, sizeof(mercury_header)};
    CHECK(mercury_reply(MERCURY_REQ, request.value, 0, &head, 1) == AP_PENDING);
    CHECK(mercury_reply(MERCURY_EVENT, 90, 0, &head, 1) == AP_PENDING);
    CHECK(mercury_disconnect(mf.client, true) == AP_OK);
    CHECK(mf.records == 3 && mf.record[1].result == AP_RECONNECT && mf.record[2].result == AP_RECONNECT);
    CHECK(mercury_stats(mf.client).pool_used == 0 && mercury_stats(mf.client).pending == 0);
    CHECK(mercury_stats(mf.client).subscriptions == 1);
    CHECK(mercury_on_packet(mf.client, 1, &mf.local) == AP_CLOSED);
    size_t sends = mf.sends; mf.send_result = AP_FULL; mf.generation = 2;
    CHECK(mercury_connect(mf.client, 2) == AP_OK && mf.sends == sends);
    CHECK(mercury_next_wait_ms(mf.client) == UINT32_MAX); /* Wait for writable, no spin. */
    mf.send_result = AP_OK;
    CHECK(mercury_tick(mf.client) == AP_OK && mf.sends == sends + 1);
    uint64_t sequence = 0;
    for (size_t i = 0; i < 8; ++i) { sequence = (sequence << 8) | mf.sent[2 + i]; }
    CHECK(sequence > request.value && mf.sent_command == MERCURY_SUB);
    CHECK(mercury_ok(MERCURY_SUB, sub) == AP_STALE && mf.records == 3);
    CHECK(mercury_ok(MERCURY_SUB, (MercuryId){sequence}) == AP_OK);
    CHECK(mf.record[3].id.value == sub.value); /* Stable subscription callback ID. */
    CHECK(mercury_ok(MERCURY_EVENT, (MercuryId){99}) == AP_OK && mf.record[4].event);
    CHECK(mercury_ok(MERCURY_REQ, request) == AP_STALE);
    CHECK(mercury_disconnect(mf.client, true) == AP_OK);
    CHECK(mercury_cancel(mf.client, sub) == AP_OK); /* Can cancel retained desire while offline. */
    CHECK(mercury_connect(mf.client, 3) == AP_OK && mercury_stats(mf.client).subscriptions == 0);
    mercury_fixture_close();
    mercury_fixture_open(false); sub = subscribe("hm://x");
    CHECK(mercury_ok(MERCURY_SUB, sub) == AP_OK);
    CHECK(mercury_disconnect(mf.client, true) == AP_OK && mercury_stats(mf.client).subscriptions == 0);
    sends = mf.sends; CHECK(mercury_connect(mf.client, 2) == AP_OK && mf.sends == sends);
    mercury_fixture_close();
    /* Pending SUB reconnects once; cancellation uses stable ID after replay. */
    mercury_fixture_open(true); sub = subscribe("hm://x");
    CHECK(mercury_disconnect(mf.client, true) == AP_OK && mf.records == 1);
    CHECK(mercury_connect(mf.client, 2) == AP_OK);
    CHECK(mercury_cancel(mf.client, sub) == AP_OK && mf.records == 2);
    CHECK(mercury_stats(mf.client).pending == 0 && mercury_stats(mf.client).subscriptions == 0);
    mercury_fixture_close();
}

static void event_subscription_generation(void)
{
    mercury_fixture_open(false); MercuryId old = subscribe("hm://x");
    CHECK(mercury_ok(MERCURY_SUB, old) == AP_OK);
    MercurySpan head = {mercury_header, sizeof(mercury_header)};
    CHECK(mercury_reply(MERCURY_EVENT, 10, 0, &head, 1) == AP_PENDING);
    CHECK(mercury_cancel(mf.client, old) == AP_OK);
    MercuryId current = subscribe("hm://x"); CHECK(mercury_ok(MERCURY_SUB, current) == AP_OK);
    size_t records = mf.records;
    CHECK(mercury_reply(MERCURY_EVENT, 10, 1, NULL, 0) == AP_STALE && mf.records == records);
    MercuryId request = mercury_get(500);
    /* Event and response sequence spaces must never steal each other's slots. */
    CHECK(mercury_ok(MERCURY_EVENT, request) == AP_OK);
    CHECK(mercury_stats(mf.client).pending == 1);
    CHECK(mercury_ok(MERCURY_REQ, request) == AP_OK);
    CHECK(mf.record[records].id.value == current.value && mf.record[records].event);
    CHECK(mf.record[records + 1].id.value == request.value && !mf.record[records + 1].event);
    mercury_fixture_close();
}

void mercury_subscription_tests(void)
{
    subscription_fixtures(); fallback_and_split_events(); subscription_limits(); reconnect();
    event_subscription_generation();
}
