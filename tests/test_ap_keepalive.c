#include "ap_session_test.h"

static void round_trip(void)
{
    ap_fixture_open(); ap_fixture_authenticated();
    ApTestFixture *t = &ap_fixture;
    ap_fixture_server_packet(AP_CMD_PING, "1234", 4, false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && t->session->keepalive == AK_PONG);
    CHECK(ap_next_wait_ms(t->session) == 60000);
    size_t written = t->fake.written;
    t->now += 59999; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_OK && t->fake.written == written);
    ++t->now; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_OK && t->fake.written == written + 11);
    CHECK(t->session->keepalive == AK_ACK && ap_next_wait_ms(t->session) == 20000);
    size_t used;
    CHECK(ap_packet_feed(&t->server, t->fake.output + written, 11, &used) == AP_OK && used == 11);
    ApPacketView pong;
    CHECK(ap_packet_peek(&t->server, &pong) == AP_OK && pong.command == AP_CMD_PONG && pong.bytes == 4);
    CHECK(ap_test_zero(pong.body, pong.bytes)); CHECK(ap_packet_consume(&t->server) == AP_OK);
    ap_fixture_server_packet(AP_CMD_PONG_ACK, NULL, 0, false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && t->session->keepalive == AK_PING);
    CHECK(ap_next_wait_ms(t->session) == 80000 && t->fake.callbacks == 3);
    t->now += 80000;
    CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_TIMEOUT && ap_state(t->session) == AP_STATE_BACKOFF);
    ap_fixture_close();
}

static void deadlines_and_backpressure(void)
{
    /* Initial ping and ACK deadlines are independent of UI work. */
    ap_fixture_open(); ap_fixture_authenticated();
    ap_fixture.now += 20000;
    CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_TIMEOUT && ap_result(ap_fixture.session) == AP_TIMEOUT);
    ap_fixture_close();
    ap_fixture_open(); ap_fixture_authenticated();
    ApTestFixture *t = &ap_fixture;
    ap_fixture_server_packet(AP_CMD_PING, "1234", 4, false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    t->now += 60000;
    CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_OK && t->session->keepalive == AK_ACK);
    t->now += 20000;
    CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_TIMEOUT); ap_fixture_close();
    /* A blocked application frame must not create a zero-time Pong polling
     * loop. Its finite TX deadline bounds the pending Pong instead. */
    ap_fixture_open(); ap_fixture_authenticated(); t = &ap_fixture;
    ap_fixture_server_packet(AP_CMD_PING, "1234", 4, false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    t->now += 59999; t->fake.write_limit = t->fake.written;
    CHECK(ap_send(t->session, 0xb2, "fixture", 7) == AP_OK && t->session->packet.tx_bytes != 0);
    ++t->now; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_PENDING);
    CHECK(t->session->pong_pending && ap_next_wait_ms(t->session) == 4999);
    t->now += 4999; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_TIMEOUT); ap_fixture_close();
    ap_fixture_open(); ap_fixture_authenticated(); t = &ap_fixture;
    ap_fixture_server_packet(AP_CMD_PING, "1234", 4, false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    t->now += 59999; t->fake.write_limit = t->fake.written;
    CHECK(ap_send(t->session, 0xb2, "fixture", 7) == AP_OK);
    ++t->now; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_PENDING && t->session->pong_pending);
    t->fake.write_limit = sizeof(t->fake.output);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && ap_next_wait_ms(t->session) == 0);
    CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_OK && !t->session->pong_pending && t->session->keepalive == AK_ACK);
    CHECK(ap_next_wait_ms(t->session) == 20000); ap_fixture_close();
}

static void cancellation_phases(void)
{
    const ApState phases[] = {AP_STATE_CONNECT, AP_STATE_HELLO, AP_STATE_CHALLENGE,
        AP_STATE_FRAMED, AP_STATE_AUTHENTICATED};
    for (size_t i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        ap_fixture_open(); ApTestFixture *t = &ap_fixture;
        if (phases[i] == AP_STATE_FRAMED) { ap_fixture_framed(); }
        else if (phases[i] == AP_STATE_AUTHENTICATED) { ap_fixture_authenticated(); }
        else {
            ap_fixture_start();
            if (phases[i] == AP_STATE_HELLO) { t->fake.write_limit = 0; }
            if (phases[i] == AP_STATE_CHALLENGE) { t->fake.available = 0; }
            if (phases[i] != AP_STATE_CONNECT) { CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING); }
        }
        CHECK(ap_state(t->session) == phases[i]);
        CHECK(ap_fixture_event(AP_EVENT_CANCEL) == AP_CANCELLED && ap_state(t->session) == AP_STATE_CLOSED);
        CHECK(ap_result(t->session) == AP_CANCELLED && ap_next_wait_ms(t->session) == INFINITE);
        CHECK(ap_test_zero(&t->session->packet, sizeof(t->session->packet)) &&
            ap_test_zero(&t->session->handshake, sizeof(t->session->handshake)) && crypto_balanced(&t->session->crypto));
        ap_fixture_close();
    }
    ap_fixture_open(); ap_fixture_start(); ApTestFixture *t = &ap_fixture;
    t->fake.write_limit = sizeof(av_hello) + 3;
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && t->session->handshake.complete);
    CHECK(ap_fixture_event(AP_EVENT_CANCEL) == AP_CANCELLED); ap_fixture_close();
}

static void auth_and_partial_frames(void)
{
    const uint8_t failures[][2] = {{0x50, 12}, {0x50, 2}};
    for (size_t i = 0; i < 2; ++i) {
        ap_fixture_open(); ap_fixture_framed(); ApTestFixture *t = &ap_fixture;
        CHECK(ap_send(t->session, AP_CMD_LOGIN, av_login, sizeof(av_login)) == AP_OK);
        ap_fixture_server_packet(AP_CMD_AUTH_FAILURE, failures[i], 2, false);
        CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_AUTH_FAILED && t->fake.callbacks == 1);
        CHECK(ap_state(t->session) == (i == 0 ? AP_STATE_CLOSED : AP_STATE_BACKOFF));
        ap_fixture_close();
    }
    ap_fixture_open(); ap_fixture_framed(); ApTestFixture *t = &ap_fixture;
    CHECK(ap_send(t->session, AP_CMD_LOGIN, av_login, sizeof(av_login)) == AP_OK);
    ap_fixture_server_packet(AP_CMD_WELCOME, av_welcome, sizeof(av_welcome), false);
    --t->fake.available; /* withhold the final MAC byte */
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && t->fake.callbacks == 0);
    CHECK(ap_state(t->session) == AP_STATE_FRAMED && t->session->packet.rx_bytes != 0);
    t->now += 10000; CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_TIMEOUT && t->fake.callbacks == 0);
    ap_fixture_close();
    ap_fixture_open(); ap_fixture_start(); t = &ap_fixture;
    t->now += 5000;
    CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_TIMEOUT && ap_state(t->session) == AP_STATE_BACKOFF);
    ap_fixture_close();
    ap_fixture_open(); ap_fixture_authenticated();
    ap_fixture.session->packet.tx_cipher.next_nonce = UINT32_MAX;
    CHECK(ap_send(ap_fixture.session, 0xb2, NULL, 0) == AP_RECONNECT);
    CHECK(ap_state(ap_fixture.session) == AP_STATE_BACKOFF); ap_fixture_close();
}

void test_ap_keepalive(void)
{
    round_trip(); deadlines_and_backpressure(); cancellation_phases(); auth_and_partial_frames();
}
