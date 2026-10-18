#include "ap_internal.h"
#include "proto_codec.h"
#include "authentication.pb.h"
#include "keyexchange.pb.h"
#include "mercury.h"

ApResult ap_dispatch_mercury(ApSession *s, MercuryClient *client)
{
    if (s == NULL || client == NULL) { return AP_INVALID; }
    if (s->state != AP_STATE_AUTHENTICATED) { return AP_BAD_STATE; }
    return mercury_on_packet(client, s->generation, &s->packet);
}

ApResult ap_send(ApSession *s, uint8_t command, const void *body, size_t bytes)
{
    if (s == NULL || (body == NULL && bytes != 0)) { return AP_INVALID; }
    if (s->destroyed) { return AP_CLOSED; }
    if (bytes > AP_PAYLOAD_MAX) { return AP_LIMIT; }
    if (s->state == AP_STATE_FRAMED) {
        if (command != AP_CMD_LOGIN || s->login_sent) { return AP_BAD_STATE; }
        ClientResponseEncrypted login = ClientResponseEncrypted_init_zero;
        ProtoResult decoded = proto_decode(ClientResponseEncrypted_fields, body, bytes, &login);
        SecureZeroMemory(&login, sizeof(login));
        if (decoded != PROTO_OK) { return AP_MALFORMED; }
    } else if (s->state != AP_STATE_AUTHENTICATED || command == AP_CMD_LOGIN ||
        command == AP_CMD_WELCOME || command == AP_CMD_AUTH_FAILURE) { return AP_BAD_STATE; }
    ApResult result = ap_packet_send(&s->packet, command, body, bytes);
    if (result == AP_RECONNECT) { return ap_close(s, result, true); }
    if (result != AP_OK) { return result; }
    if (command == AP_CMD_LOGIN) { s->login_sent = true; }
    s->tx_deadline = core_deadline_after(core_clock_now(&s->config.clock), 5000);
    result = ap_flush_tx(s);
    return result == AP_PENDING ? AP_OK : result;
}

ApResult ap_route_packet(ApSession *s)
{
    ApPacketView view;
    if (ap_packet_peek(&s->packet, &view) != AP_OK) { return AP_BAD_STATE; }
    uint64_t now = core_clock_now(&s->config.clock);
    if (view.command == AP_CMD_WELCOME) {
        if (s->state != AP_STATE_FRAMED || !s->login_sent) { return ap_close(s, AP_BAD_STATE, false); }
        APWelcome welcome = APWelcome_init_zero;
        ProtoResult result = proto_decode(APWelcome_fields, view.body, view.bytes, &welcome);
        bool valid = result == PROTO_OK && welcome.canonical_username[0] != '\0' && welcome.reusable_auth_credentials.size != 0;
        ApResult accepted = valid ? ap_auth_accept(s, &welcome) : AP_MALFORMED;
        SecureZeroMemory(&welcome, sizeof(welcome));
        if (accepted == AP_RECONNECT) {
            /* A fresh handshake is required for the returned stored credential.
             * This one-time transition has its own retry budget. */
            s->attempts = 0;
            return ap_close(s, AP_RECONNECT, true);
        }
        if (accepted != AP_OK) { return ap_close(s, accepted, false); }
        s->state = AP_STATE_AUTHENTICATED; s->result = AP_OK; s->deadline = CORE_CLOCK_NEVER;
        s->keepalive = AK_PING; s->keepalive_at = core_deadline_after(now, 20000);
    } else if (view.command == AP_CMD_AUTH_FAILURE) {
        if (s->state != AP_STATE_FRAMED || !s->login_sent) { return ap_close(s, AP_BAD_STATE, false); }
        APLoginFailed failure = APLoginFailed_init_zero;
        if (proto_decode(APLoginFailed_fields, view.body, view.bytes, &failure) != PROTO_OK) {
            return ap_close(s, AP_MALFORMED, false);
        }
        bool retry = failure.error_code == ErrorCode_TryAnotherAP;
        if (s->config.on_packet != NULL) { s->config.on_packet(s->config.context, s, &view); }
        return ap_close(s, AP_AUTH_FAILED, retry);
    } else if (s->state != AP_STATE_AUTHENTICATED) { return ap_close(s, AP_BAD_STATE, false); }
    else if (view.command == AP_CMD_PING) {
        if (view.bytes != 4 || s->keepalive != AK_PING) { return ap_close(s, AP_MALFORMED, false); }
        s->keepalive = AK_PONG; s->keepalive_at = core_deadline_after(now, 60000);
    } else if (view.command == AP_CMD_PONG_ACK) {
        if ((view.bytes != 0 && view.bytes != 4) || s->keepalive != AK_ACK) { return ap_close(s, AP_BAD_STATE, false); }
        s->keepalive = AK_PING; s->keepalive_at = core_deadline_after(now, 80000);
    } else if (view.command == AP_CMD_LOGIN) { return ap_close(s, AP_BAD_STATE, false); }
    if (s->config.on_packet != NULL) { s->config.on_packet(s->config.context, s, &view); }
    return AP_OK;
}

static ApResult queue_pong(ApSession *s, uint64_t now)
{
    static const uint8_t zero[4] = {0};
    ApResult result = ap_packet_send(&s->packet, AP_CMD_PONG, zero, sizeof(zero));
    if (result == AP_FULL) {
        /* TX's finite deadline protects this wait; don't spin on an expired
         * Pong deadline while FD_WRITE is blocked. */
        s->pong_pending = true; s->keepalive_at = CORE_CLOCK_NEVER; return AP_PENDING;
    }
    if (result != AP_OK) { return ap_close(s, result, result == AP_RECONNECT); }
    s->pong_pending = false; s->pong_queued = true; s->keepalive_at = CORE_CLOCK_NEVER;
    s->tx_deadline = core_deadline_after(now, 5000);
    return ap_flush_tx(s);
}

ApResult ap_timers(ApSession *s)
{
    uint64_t now = core_clock_now(&s->config.clock);
    if (s->state == AP_STATE_CLOSED || s->state == AP_STATE_BACKOFF) { return AP_OK; }
    if (core_deadline_expired(now, s->deadline) || core_deadline_expired(now, s->frame_deadline) ||
        core_deadline_expired(now, s->tx_deadline)) {
        return ap_close(s, AP_TIMEOUT, s->state != AP_STATE_RESOLVE && s->state != AP_STATE_FRAMED);
    }
    if (s->state != AP_STATE_AUTHENTICATED) { return AP_OK; }
    if (s->pong_pending && s->packet.tx_bytes == 0) { return queue_pong(s, now); }
    if (core_deadline_expired(now, s->keepalive_at)) {
        if (s->keepalive != AK_PONG) { return ap_close(s, AP_TIMEOUT, true); }
        return queue_pong(s, now);
    }
    return AP_OK;
}
