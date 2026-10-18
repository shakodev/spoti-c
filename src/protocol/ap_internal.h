#ifndef SPOTIC_PROTOCOL_AP_INTERNAL_H
#define SPOTIC_PROTOCOL_AP_INTERNAL_H
#include "ap_socket.h"
#include "ap_handshake.h"
#include "core/memory_budget.h"

typedef enum ApKeepalive { AK_PING, AK_PONG, AK_ACK } ApKeepalive;
struct ApSession {
    ApConfig config;
    ApState state;
    ApResult result;
    ApTransport io;
    ApSocket socket;
    Crypto crypto;
    CryptoDh dh;
    ApPacket packet;
    ApHandshake handshake;
    ApEndpoints endpoints;
    HttpRequestId resolve_id;
    size_t tx_offset;
    uint64_t deadline, frame_deadline, keepalive_at, tx_deadline;
    uint32_t generation, attempts;
    uint8_t nonce[16];
    ApKeepalive keepalive;
    bool native_io, http_active, io_stopping, login_sent, io_ready, in_event, destroyed;
    bool pong_pending, pong_queued;
    bool shutdown_started;
};
_Static_assert(sizeof(ApSession) <= MEMORY_BUDGET_PROTOCOL, "AP shared protocol storage cap");

ApResult ap_close(ApSession *session, ApResult result, bool retry);
ApResult ap_retire(ApSession *session);
ApResult ap_connect(ApSession *session);
ApResult ap_drive_io(ApSession *session);
ApResult ap_route_packet(ApSession *session);
ApResult ap_timers(ApSession *session);
ApResult ap_resolve_event(ApSession *session);
ApResult ap_flush_tx(ApSession *session);
#endif
