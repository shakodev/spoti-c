#include "ap_session_test.h"

ApTestFixture ap_fixture;
static _Alignas(ApSession) uint8_t protocol_memory[MEMORY_BUDGET_PROTOCOL];

static ApResult begin(void *context, const ApEndpoint *endpoint)
{
    FakeAp *f = context;
    CHECK(f->begins < 8); f->chosen[f->begins++] = *endpoint;
    f->connected = false; f->read_at = f->written = 0;
    return f->begins <= f->fail_begins ? AP_IO_ERROR : AP_PENDING;
}
static ApResult poll(void *context)
{
    FakeAp *f = context; ResetEvent(f->wake);
    return f->connected ? AP_OK : AP_PENDING;
}
static ApResult read_bytes(void *context, void *output, size_t capacity, size_t *bytes)
{
    FakeAp *f = context; *bytes = 0;
    if (f->read_at == f->available) { return f->eof ? AP_CLOSED : AP_PENDING; }
    CHECK(f->written >= sizeof(av_hello));
    size_t n = f->available - f->read_at;
    if (n > capacity) { n = capacity; }
    if (n > f->fragment) { n = f->fragment; }
    memcpy(output, f->input + f->read_at, n); f->read_at += n; *bytes = n; return AP_OK;
}
static ApResult write_bytes(void *context, const void *bytes, size_t size, size_t *sent)
{
    FakeAp *f = context; *sent = 0;
    if (f->written >= f->write_limit) { return AP_PENDING; }
    size_t n = size < f->fragment ? size : f->fragment;
    if (n > f->write_limit - f->written) { n = f->write_limit - f->written; }
    CHECK(n != 0 && n <= sizeof(f->output) - f->written);
    memcpy(f->output + f->written, bytes, n); f->written += n; *sent = n; return AP_OK;
}
static ApResult stop(void *context)
{
    FakeAp *f = context; ++f->stops;
    if (f->stop_pending && !f->stop_released) { return AP_PENDING; }
    f->connected = false; ResetEvent(f->wake); return AP_OK;
}
static size_t handles(void *context, HANDLE *output, size_t capacity)
{
    FakeAp *f = context;
    if (capacity == 0) { return 0; }
    output[0] = f->wake; return 1;
}
static uint64_t now(void *context) { return *(const uint64_t *)context; }
static void on_packet(void *context, ApSession *session, const ApPacketView *view)
{
    FakeAp *f = context;
    CHECK(session->packet.verified && view->body != NULL);
    CHECK(ap_on_event(session, (ApEvent){AP_EVENT_TIMER, ap_generation(session)}) == AP_BAD_STATE);
    ++f->callbacks;
}

void ap_fixture_open(void)
{
    ApTestFixture *t = &ap_fixture; *t = (ApTestFixture){0};
    CHECK(metrics_init(&t->metrics)); CHECK(arena_init(&t->arena, protocol_memory, sizeof(protocol_memory)));
    CHECK(metrics_attach_arena(&t->metrics, &t->arena, &t->binding));
    t->now = 1000; t->fake.fragment = 1; t->fake.write_limit = sizeof(t->fake.output);
    t->fake.wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(t->fake.wake != NULL && metrics_handle_acquire(&t->metrics, &t->fake.wake_metric, (uintptr_t)t->fake.wake));
    memcpy(t->fake.input, av_response, sizeof(av_response)); t->fake.available = sizeof(av_response);
    ApConfig config = {.version = 123456789, .metrics = &t->metrics, .clock = {now, &t->now},
        .transport = {&t->fake, begin, poll, read_bytes, write_bytes, stop, handles},
        .on_packet = on_packet, .context = &t->fake};
    CHECK(ap_session_create(&t->arena, &config, &t->session) == AP_OK);
    CHECK(t->arena.used == sizeof(ApSession) && t->arena.used <= MEMORY_BUDGET_PROTOCOL);
    t->endpoints.accesspoint_count = 2;
    memcpy(t->endpoints.accesspoint[0].host, "fake-a.invalid", 15); t->endpoints.accesspoint[0].port = 4070;
    memcpy(t->endpoints.accesspoint[1].host, "fake-b.invalid", 15); t->endpoints.accesspoint[1].port = 443;
    CHECK(ap_next_wait_ms(t->session) == INFINITE);
}

void ap_fixture_close(void)
{
    ApTestFixture *t = &ap_fixture;
    CHECK(ap_session_shutdown(t->session) == AP_OK);
    CHECK(ap_session_shutdown(t->session) == AP_CLOSED && crypto_balanced(&t->session->crypto));
    CHECK(CloseHandle(t->fake.wake) && metrics_handle_release(&t->fake.wake_metric));
    ap_packet_clear(&t->server); arena_dispose(&t->arena); CHECK(metrics_balanced(&t->metrics));
}

ApResult ap_fixture_event(ApEventType type)
{
    return ap_on_event(ap_fixture.session, (ApEvent){type, ap_generation(ap_fixture.session)});
}

void ap_fixture_start(void)
{
    ApTestFixture *t = &ap_fixture;
    CHECK(ap_start(t->session, &t->endpoints) == AP_OK && ap_state(t->session) == AP_STATE_CONNECT);
    CHECK(crypto_dh_clear(&t->session->dh) == CRYPTO_OK);
    ap_test_import(&t->session->dh, &t->session->crypto, av_private, av_public);
    memcpy(t->session->nonce, av_nonce, 16); t->fake.connected = true;
}

void ap_fixture_framed(void)
{
    ApTestFixture *t = &ap_fixture;
    ap_fixture_start();
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    CHECK(ap_state(t->session) == AP_STATE_FRAMED && t->fake.callbacks == 0);
    CHECK(t->fake.written == sizeof(av_hello) + sizeof(av_reply));
    CHECK(memcmp(t->fake.output, av_hello, sizeof(av_hello)) == 0);
    CHECK(memcmp(t->fake.output + sizeof(av_hello), av_reply, sizeof(av_reply)) == 0);
    CHECK(ap_test_zero(&t->session->handshake, sizeof(t->session->handshake)) && crypto_balanced(&t->session->crypto));
    CHECK(ap_packet_init(&t->server, av_rx, av_tx) == AP_OK);
}

void ap_fixture_server_packet(uint8_t command, const void *body, size_t bytes, bool corrupt)
{
    ApTestFixture *t = &ap_fixture;
    CHECK(ap_packet_send(&t->server, command, body, bytes) == AP_OK);
    size_t n = t->server.tx_bytes;
    CHECK(n <= sizeof(t->fake.input) - t->fake.available);
    memcpy(t->fake.input + t->fake.available, t->server.tx, n);
    if (corrupt) { t->fake.input[t->fake.available + n - 1] ^= 1; }
    t->fake.available += n; CHECK(ap_packet_advance(&t->server, n) == AP_OK);
}

void ap_fixture_authenticated(void)
{
    ApTestFixture *t = &ap_fixture;
    ap_fixture_framed();
    CHECK(ap_send(t->session, 0xb2, NULL, 0) == AP_BAD_STATE);
    CHECK(ap_send(t->session, AP_CMD_LOGIN, NULL, 0) == AP_MALFORMED);
    size_t start = t->fake.written;
    CHECK(ap_send(t->session, AP_CMD_LOGIN, av_login, sizeof(av_login)) == AP_OK);
    size_t used;
    CHECK(ap_packet_feed(&t->server, t->fake.output + start, t->fake.written - start, &used) == AP_OK);
    ApPacketView view;
    CHECK(ap_packet_peek(&t->server, &view) == AP_OK && view.command == AP_CMD_LOGIN && view.bytes == sizeof(av_login));
    CHECK(memcmp(view.body, av_login, sizeof(av_login)) == 0); CHECK(ap_packet_consume(&t->server) == AP_OK);
    CHECK(ap_send(t->session, AP_CMD_LOGIN, av_login, sizeof(av_login)) == AP_BAD_STATE);
    ap_fixture_server_packet(AP_CMD_WELCOME, av_welcome, sizeof(av_welcome), false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    CHECK(ap_state(t->session) == AP_STATE_AUTHENTICATED && ap_result(t->session) == AP_OK && t->fake.callbacks == 1);
    CHECK(ap_next_wait_ms(t->session) == 20000);
}

static void invalid_mac_and_login_state(void)
{
    ap_fixture_open(); ap_fixture_authenticated();
    ap_fixture_server_packet(AP_CMD_PING, "1234", 4, true);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_BAD_MAC);
    CHECK(ap_state(ap_fixture.session) == AP_STATE_CLOSED && ap_fixture.fake.callbacks == 1);
    CHECK(ap_test_zero(&ap_fixture.session->packet, sizeof(ap_fixture.session->packet)));
    ap_fixture_close();
    ap_fixture_open(); ap_fixture_framed();
    ap_fixture_server_packet(AP_CMD_WELCOME, av_welcome, sizeof(av_welcome), false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_BAD_STATE && ap_fixture.fake.callbacks == 0);
    ap_fixture_close();
}

static void failover_and_retirement(void)
{
    ap_fixture_open(); ApTestFixture *t = &ap_fixture;
    t->fake.fail_begins = 2; t->session->config.max_attempts = 2;
    CHECK(ap_start(t->session, &t->endpoints) == AP_IO_ERROR && ap_state(t->session) == AP_STATE_BACKOFF);
    CHECK(ap_next_wait_ms(t->session) == 250);
    t->now += 249; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_OK && t->fake.begins == 1);
    ++t->now; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_IO_ERROR && t->fake.begins == 2);
    CHECK(ap_state(t->session) == AP_STATE_CLOSED && t->fake.chosen[0].port == 4070 && t->fake.chosen[1].port == 443);
    CHECK(crypto_balanced(&t->session->crypto)); ap_fixture_close();
    ap_fixture_open(); ap_fixture_start(); t = &ap_fixture;
    t->fake.stop_pending = true;
    uint32_t generation = ap_generation(t->session);
    CHECK(ap_fixture_event(AP_EVENT_CANCEL) == AP_CANCELLED && t->session->io_stopping);
    CHECK(ap_start(t->session, &t->endpoints) == AP_BAD_STATE);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    t->fake.stop_released = true;
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_OK && !t->session->io_stopping);
    CHECK(ap_start(t->session, &t->endpoints) == AP_OK && ap_generation(t->session) == generation + 1);
    CHECK(ap_on_event(t->session, (ApEvent){AP_EVENT_SOCKET, generation}) == AP_STALE);
    CHECK(ap_fixture_event(AP_EVENT_CANCEL) == AP_CANCELLED);
    t->session->generation = UINT32_MAX;
    CHECK(ap_start(t->session, &t->endpoints) == AP_EXHAUSTED);
    ap_fixture_close();
    ap_fixture_open(); ap_fixture_start(); t = &ap_fixture;
    t->fake.stop_pending = true;
    CHECK(ap_session_shutdown(t->session) == AP_PENDING);
    CHECK(ap_start(t->session, &t->endpoints) == AP_CLOSED);
    t->fake.stop_released = true;
    ap_fixture_close();
}

void test_ap_session(void)
{
    invalid_mac_and_login_state(); failover_and_retirement(); test_ap_keepalive();
}
