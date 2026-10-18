#include "ap_internal.h"
#include <string.h>

static ApResult plain_write(ApSession *s, const uint8_t *bytes, size_t size)
{
    while (s->tx_offset < size) {
        size_t sent = 0;
        ApResult result = s->io.write(s->io.context, bytes + s->tx_offset, size - s->tx_offset, &sent);
        if (result == AP_PENDING && sent == 0) { return AP_PENDING; }
        if (result != AP_OK || sent == 0 || sent > size - s->tx_offset) { return ap_close(s, AP_IO_ERROR, true); }
        s->tx_offset += sent;
    }
    s->tx_offset = 0; return AP_OK;
}

ApResult ap_flush_tx(ApSession *s)
{
    const uint8_t *bytes;
    size_t size;
    while (ap_packet_tx(&s->packet, &bytes, &size) == AP_OK) {
        size_t sent = 0;
        ApResult result = s->io.write(s->io.context, bytes, size, &sent);
        if (result == AP_PENDING && sent == 0) { return AP_PENDING; }
        if (result != AP_OK || sent == 0 || sent > size) { return ap_close(s, AP_IO_ERROR, true); }
        (void)ap_packet_advance(&s->packet, sent);
    }
    s->tx_deadline = CORE_CLOCK_NEVER;
    if (s->pong_queued) {
        s->pong_queued = false; s->keepalive = AK_ACK;
        s->keepalive_at = core_deadline_after(core_clock_now(&s->config.clock), 20000);
    }
    return AP_OK;
}

static ApResult challenge_read(ApSession *s)
{
    uint8_t input[8192];
    size_t budget = AP_HANDSHAKE_MAX;
    while (!s->handshake.complete && budget != 0) {
        size_t capacity = s->handshake.expected - s->handshake.received;
        if (capacity > sizeof(input)) { capacity = sizeof(input); }
        if (capacity > budget) { capacity = budget; }
        size_t bytes = 0;
        ApResult result = s->io.read(s->io.context, input, capacity, &bytes);
        if (result == AP_PENDING && bytes == 0) { return AP_PENDING; }
        if (result != AP_OK || bytes == 0 || bytes > capacity) { return ap_close(s, AP_IO_ERROR, true); }
        budget -= bytes;
        size_t used; CryptoSessionKeys keys;
        result = ap_handshake_feed(&s->handshake, input, bytes, &used, &keys);
        if (result == AP_OK) {
            result = ap_packet_init(&s->packet, keys.tx, keys.rx);
            SecureZeroMemory(&keys, sizeof(keys));
            if (crypto_dh_clear(&s->dh) != CRYPTO_OK) { result = AP_IO_ERROR; }
            if (result != AP_OK) { return ap_close(s, result, false); }
        } else {
            SecureZeroMemory(&keys, sizeof(keys));
            if (result != AP_PENDING) { return ap_close(s, result, result == AP_RECONNECT); }
        }
        if (used != bytes) { return ap_close(s, AP_MALFORMED, false); }
    }
    return s->handshake.complete ? AP_OK : AP_PENDING;
}

static ApResult framed_read(ApSession *s)
{
    uint8_t input[8192];
    size_t budget = AP_WIRE_MAX;
    while (budget != 0) {
        size_t capacity = s->packet.rx_expected - s->packet.rx_bytes;
        if (capacity > sizeof(input)) { capacity = sizeof(input); }
        if (capacity > budget) { capacity = budget; }
        size_t bytes = 0;
        ApResult result = s->io.read(s->io.context, input, capacity, &bytes);
        if (result == AP_PENDING && bytes == 0) { return AP_PENDING; }
        if (result != AP_OK || bytes == 0 || bytes > capacity) { return ap_close(s, AP_IO_ERROR, true); }
        budget -= bytes;
        if (s->packet.rx_bytes == 0) {
            s->frame_deadline = core_deadline_after(core_clock_now(&s->config.clock), s->config.handshake_timeout_ms);
        }
        size_t used;
        result = ap_packet_feed(&s->packet, input, bytes, &used);
        if (result != AP_OK && result != AP_PENDING) { return ap_close(s, result, result == AP_RECONNECT); }
        if (used != bytes) { return ap_close(s, AP_MALFORMED, false); }
        if (result == AP_OK) {
            s->frame_deadline = CORE_CLOCK_NEVER;
            result = ap_route_packet(s);
            if (s->state != AP_STATE_FRAMED && s->state != AP_STATE_AUTHENTICATED) { return result; }
            if (result != AP_OK) { return result; }
            (void)ap_packet_consume(&s->packet);
        }
    }
    return AP_OK;
}

ApResult ap_drive_io(ApSession *s)
{
    if (s->state == AP_STATE_HELLO) {
        ApResult result = plain_write(s, s->handshake.transcript, s->handshake.hello_bytes);
        if (result != AP_OK) { return result; }
        s->state = AP_STATE_CHALLENGE;
    }
    if (s->state == AP_STATE_CHALLENGE) {
        if (!s->handshake.complete) {
            ApResult result = challenge_read(s);
            if (result != AP_OK) { return result; }
        }
        ApResult result = plain_write(s, s->handshake.reply, s->handshake.reply_bytes);
        if (result != AP_OK) { return result; }
        ap_handshake_clear(&s->handshake);
        s->state = AP_STATE_FRAMED;
        s->deadline = core_deadline_after(core_clock_now(&s->config.clock), s->config.auth_timeout_ms);
    }
    if (s->state == AP_STATE_FRAMED || s->state == AP_STATE_AUTHENTICATED) {
        if (s->state == AP_STATE_FRAMED) {
            ApResult auth = ap_auth_send(s);
            if (auth != AP_OK) { return auth; }
        }
        ApResult result = ap_flush_tx(s);
        if (result != AP_OK && result != AP_PENDING) { return result; }
        return framed_read(s);
    }
    return AP_BAD_STATE;
}
