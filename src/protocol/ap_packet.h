#ifndef SPOTIC_PROTOCOL_AP_PACKET_H
#define SPOTIC_PROTOCOL_AP_PACKET_H
#include "shannon_adapter.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AP_PAYLOAD_MAX ((size_t)65535)
#define AP_WIRE_MAX (AP_PAYLOAD_MAX + 7)
typedef enum ApResult {
    AP_OK, AP_PENDING, AP_FULL, AP_INVALID, AP_BAD_STATE, AP_LIMIT, AP_NO_MEMORY,
    AP_IO_ERROR, AP_CLOSED, AP_CANCELLED, AP_TIMEOUT, AP_BAD_SIGNATURE,
    AP_BAD_MAC, AP_MALFORMED, AP_UNSUPPORTED, AP_AUTH_FAILED, AP_RECONNECT,
    AP_STALE, AP_EXHAUSTED
} ApResult;
enum { AP_CMD_PING = 0x04, AP_CMD_PONG = 0x49, AP_CMD_PONG_ACK = 0x4a,
       AP_CMD_LOGIN = 0xab, AP_CMD_WELCOME = 0xac, AP_CMD_AUTH_FAILURE = 0xad };
typedef struct ApPacket {
    ShannonAdapter tx_cipher, rx_cipher;
    uint8_t tx[AP_WIRE_MAX], rx[AP_WIRE_MAX];
    size_t tx_bytes, tx_offset, rx_bytes, rx_expected;
    bool initialized, verified;
    ApResult failure;
} ApPacket;
typedef struct ApPacketView { uint8_t command; const uint8_t *body; size_t bytes; } ApPacketView;

/* Start at {0}. Single network owner, fixed separate TX/RX buffers. Clear before reinit;
 * keys must not overlap the context. No allocation or I/O. */
ApResult ap_packet_init(ApPacket *packet, const uint8_t tx_key[32], const uint8_t rx_key[32]);
void ap_packet_clear(ApPacket *packet);
ApResult ap_packet_send(ApPacket *packet, uint8_t command, const void *body, size_t bytes);
/* Borrow ciphertext until advance/clear. Preserve partial sends exactly. */
ApResult ap_packet_tx(const ApPacket *packet, const uint8_t **bytes, size_t *size);
ApResult ap_packet_advance(ApPacket *packet, size_t sent);
/* Consumes at most one frame; caller retains the unconsumed suffix. No output
 * is visible until MAC success. FULL means consume verified RX before feeding.
 * Any authentication/nonce failure wipes both ciphers and pending buffers. */
ApResult ap_packet_feed(ApPacket *packet, const void *bytes, size_t size, size_t *consumed);
ApResult ap_packet_peek(const ApPacket *packet, ApPacketView *view);
ApResult ap_packet_consume(ApPacket *packet);
#endif
