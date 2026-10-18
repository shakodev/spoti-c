#include "ap_packet.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

void ap_packet_clear(ApPacket *packet)
{
    if (packet != NULL) { SecureZeroMemory(packet, sizeof(*packet)); }
}

static ApResult fail(ApPacket *packet, ApResult result)
{
    ap_packet_clear(packet); packet->failure = result; return result;
}

ApResult ap_packet_init(ApPacket *packet, const uint8_t tx[32], const uint8_t rx[32])
{
    if (packet == NULL || tx == NULL || rx == NULL) { return AP_INVALID; }
    if (packet->initialized) { return AP_BAD_STATE; }
    ap_packet_clear(packet);
    if (shannon_adapter_init(&packet->tx_cipher, SHANNON_TX, tx, 32) != SHANNON_OK ||
        shannon_adapter_init(&packet->rx_cipher, SHANNON_RX, rx, 32) != SHANNON_OK) {
        return fail(packet, AP_BAD_STATE);
    }
    packet->initialized = true; packet->rx_expected = 3; return AP_OK;
}

ApResult ap_packet_send(ApPacket *packet, uint8_t command, const void *body, size_t bytes)
{
    if (packet == NULL || (bytes != 0 && body == NULL)) { return AP_INVALID; }
    if (!packet->initialized) { return packet->failure != AP_OK ? packet->failure : AP_BAD_STATE; }
    if (bytes > AP_PAYLOAD_MAX) { return AP_LIMIT; }
    if (packet->tx_bytes != 0) { return AP_FULL; }
    if (packet->tx_cipher.next_nonce >= UINT32_MAX) { return fail(packet, AP_RECONNECT); }
    if (bytes != 0) { memmove(packet->tx + 3, body, bytes); }
    packet->tx[0] = command; packet->tx[1] = (uint8_t)(bytes >> 8); packet->tx[2] = (uint8_t)bytes;
    if (shannon_adapter_begin(&packet->tx_cipher) != SHANNON_OK ||
        shannon_adapter_update(&packet->tx_cipher, packet->tx, bytes + 3) != SHANNON_OK ||
        shannon_adapter_finish_tx(&packet->tx_cipher, packet->tx + bytes + 3) != SHANNON_OK) {
        return fail(packet, AP_BAD_STATE);
    }
    packet->tx_bytes = bytes + 7; packet->tx_offset = 0; return AP_OK;
}

ApResult ap_packet_tx(const ApPacket *packet, const uint8_t **bytes, size_t *size)
{
    if (bytes == NULL || size == NULL) { return AP_INVALID; }
    *bytes = NULL; *size = 0;
    if (packet == NULL) { return AP_INVALID; }
    if (!packet->initialized) { return packet->failure != AP_OK ? packet->failure : AP_BAD_STATE; }
    if (packet->tx_bytes == 0) { return AP_PENDING; }
    *bytes = packet->tx + packet->tx_offset; *size = packet->tx_bytes - packet->tx_offset;
    return AP_OK;
}

ApResult ap_packet_advance(ApPacket *packet, size_t sent)
{
    if (packet == NULL) { return AP_INVALID; }
    if (!packet->initialized || packet->tx_bytes == 0) { return AP_BAD_STATE; }
    if (sent > packet->tx_bytes - packet->tx_offset) { return AP_INVALID; }
    packet->tx_offset += sent;
    if (packet->tx_offset == packet->tx_bytes) {
        SecureZeroMemory(packet->tx, packet->tx_bytes); packet->tx_bytes = packet->tx_offset = 0;
    }
    return AP_OK;
}

ApResult ap_packet_feed(ApPacket *packet, const void *bytes, size_t size, size_t *consumed)
{
    if (consumed == NULL) { return AP_INVALID; }
    *consumed = 0;
    if (packet == NULL || (bytes == NULL && size != 0)) { return AP_INVALID; }
    if (!packet->initialized) { return packet->failure != AP_OK ? packet->failure : AP_BAD_STATE; }
    if (packet->verified) { return AP_FULL; }
    if (size == 0) { return AP_PENDING; }
    if (packet->rx_bytes == 0) {
        if (packet->rx_cipher.next_nonce >= UINT32_MAX) { return fail(packet, AP_RECONNECT); }
        if (shannon_adapter_begin(&packet->rx_cipher) != SHANNON_OK) { return fail(packet, AP_BAD_STATE); }
    }
    const uint8_t *source = bytes;
    while (*consumed < size && packet->rx_bytes < packet->rx_expected) {
        size_t start = packet->rx_bytes;
        size_t take = packet->rx_expected - start;
        if (take > size - *consumed) { take = size - *consumed; }
        memcpy(packet->rx + start, source + *consumed, take);
        packet->rx_bytes += take; *consumed += take;
        /* MAC bytes are copied but never decrypted. Header is decrypted only
         * once, before determining the body length. */
        size_t end = packet->rx_expected == 3 ? 3 : packet->rx_expected - 4;
        size_t decrypt = start < end ? (take < end - start ? take : end - start) : 0;
        if (shannon_adapter_update(&packet->rx_cipher, packet->rx + start, decrypt) != SHANNON_OK) {
            return fail(packet, AP_BAD_STATE);
        }
        if (packet->rx_bytes == 3 && packet->rx_expected == 3) {
            packet->rx_expected = 7 + ((size_t)packet->rx[1] << 8) + packet->rx[2];
        }
    }
    if (packet->rx_bytes != packet->rx_expected) { return AP_PENDING; }
    if (shannon_adapter_finish_rx(&packet->rx_cipher, packet->rx + packet->rx_expected - 4) != SHANNON_OK) {
        return fail(packet, AP_BAD_MAC);
    }
    packet->verified = true; return AP_OK;
}

ApResult ap_packet_peek(const ApPacket *packet, ApPacketView *view)
{
    if (view == NULL) { return AP_INVALID; }
    *view = (ApPacketView){0};
    if (packet == NULL) { return AP_INVALID; }
    if (!packet->initialized) { return packet->failure != AP_OK ? packet->failure : AP_BAD_STATE; }
    if (!packet->verified) { return AP_PENDING; }
    *view = (ApPacketView){packet->rx[0], packet->rx + 3, packet->rx_expected - 7}; return AP_OK;
}

ApResult ap_packet_consume(ApPacket *packet)
{
    if (packet == NULL) { return AP_INVALID; }
    if (!packet->verified) { return AP_BAD_STATE; }
    SecureZeroMemory(packet->rx, packet->rx_bytes);
    packet->rx_bytes = 0; packet->rx_expected = 3; packet->verified = false; return AP_OK;
}
