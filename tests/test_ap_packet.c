#include "protocol/ap_packet.h"
#include "shannon_test.h"
#include "shannon_vectors.h"

static ApPacket packet;
static uint8_t wire[AP_WIRE_MAX + 1];

static void known_frames(void)
{
    for (size_t i = 0; i < sizeof(shannon_vectors) / sizeof(shannon_vectors[0]); ++i) {
        const ShannonVector *v = &shannon_vectors[i];
        if (strncmp(v->name, "frame-", 6) != 0) { continue; }
        CHECK(v->size >= 3 && (((size_t)v->plaintext[1] << 8) | v->plaintext[2]) == v->size - 3);
        ap_packet_clear(&packet);
        CHECK(ap_packet_init(&packet, v->key, v->key) == AP_OK);
        packet.tx_cipher.next_nonce = packet.rx_cipher.next_nonce = vector_nonce(v);
        CHECK(ap_packet_send(&packet, v->plaintext[0], v->plaintext + 3, v->size - 3) == AP_OK);
        const uint8_t *out; size_t bytes;
        CHECK(ap_packet_tx(&packet, &out, &bytes) == AP_OK && bytes == v->size + 4);
        CHECK(memcmp(out, v->ciphertext, v->size) == 0 && memcmp(out + v->size, v->mac, 4) == 0);
        memcpy(wire, out, bytes); wire[bytes] = 0xa5;
        CHECK(ap_packet_send(&packet, 0, NULL, 0) == AP_FULL);
        for (size_t p = 0; p < bytes; ++p) {
            size_t consumed;
            ApResult result = ap_packet_feed(&packet, wire + p, 1, &consumed);
            CHECK(consumed == 1 && result == (p + 1 == bytes ? AP_OK : AP_PENDING));
            ApPacketView view;
            CHECK(ap_packet_peek(&packet, &view) == (p + 1 == bytes ? AP_OK : AP_PENDING));
            if (p + 1 != bytes) { CHECK(view.body == NULL); }
            CHECK(ap_packet_advance(&packet, 1) == AP_OK);
        }
        CHECK(wire[bytes] == 0xa5 && ap_packet_tx(&packet, &out, &bytes) == AP_PENDING);
        ApPacketView view;
        CHECK(ap_packet_peek(&packet, &view) == AP_OK && view.command == v->plaintext[0]);
        CHECK(view.bytes == v->size - 3 && memcmp(view.body, v->plaintext + 3, view.bytes) == 0);
        size_t consumed;
        CHECK(ap_packet_feed(&packet, wire, 1, &consumed) == AP_FULL && consumed == 0);
        CHECK(ap_packet_consume(&packet) == AP_OK && all_zero(packet.rx, sizeof(packet.rx)));
    }
}

static void tampering_and_limits(void)
{
    uint8_t key[32] = {0};
    ap_packet_clear(&packet);
    CHECK(ap_packet_init(&packet, key, key) == AP_OK);
    CHECK(ap_packet_send(&packet, 0x04, "ping", 4) == AP_OK);
    size_t bytes = packet.tx_bytes; memcpy(wire, packet.tx, bytes);
    for (size_t pos = 0; pos < bytes; ++pos) {
        if (pos == 1 || pos == 2) { continue; }
        ap_packet_clear(&packet); CHECK(ap_packet_init(&packet, key, key) == AP_OK);
        wire[pos] ^= 1;
        size_t consumed; CHECK(ap_packet_feed(&packet, wire, bytes, &consumed) == AP_BAD_MAC);
        CHECK(!packet.initialized && all_zero(packet.rx, sizeof(packet.rx)) && all_zero(packet.tx, sizeof(packet.tx)));
        ApPacketView view; CHECK(ap_packet_peek(&packet, &view) == AP_BAD_MAC && view.body == NULL);
        CHECK(ap_packet_send(&packet, 0, NULL, 0) == AP_BAD_MAC); wire[pos] ^= 1;
    }
    ap_packet_clear(&packet); CHECK(ap_packet_init(&packet, key, key) == AP_OK);
    wire[1] ^= 1;
    size_t used;
    CHECK(ap_packet_feed(&packet, wire, bytes, &used) != AP_OK && used <= bytes);
    ApPacketView quarantine;
    CHECK(ap_packet_peek(&packet, &quarantine) != AP_OK && quarantine.body == NULL && packet.rx_expected <= AP_WIRE_MAX);
    wire[1] ^= 1;
    ap_packet_clear(&packet); CHECK(ap_packet_init(&packet, key, key) == AP_OK);
    CHECK(ap_packet_send(&packet, 0, wire, AP_PAYLOAD_MAX + 1) == AP_LIMIT && packet.tx_cipher.next_nonce == 0);
    CHECK(ap_packet_send(&packet, 0, NULL, 1) == AP_INVALID);
    CHECK(ap_packet_init(&packet, key, key) == AP_BAD_STATE);
    packet.tx_cipher.next_nonce = UINT32_MAX - 1;
    CHECK(ap_packet_send(&packet, 0, NULL, 0) == AP_OK && packet.tx_cipher.next_nonce == UINT32_MAX);
    CHECK(ap_packet_advance(&packet, packet.tx_bytes) == AP_OK);
    CHECK(ap_packet_send(&packet, 0, NULL, 0) == AP_RECONNECT && !packet.initialized);
    CHECK(ap_packet_init(&packet, key, key) == AP_OK);
    packet.rx_cipher.next_nonce = UINT32_MAX;
    size_t consumed; CHECK(ap_packet_feed(&packet, wire, 1, &consumed) == AP_RECONNECT && consumed == 0);
    ap_packet_clear(&packet);
    CHECK(all_zero(&packet, sizeof(packet)));
}

static void coalesced_and_fragmented(void)
{
    static ApPacket remote;
    uint8_t key_a[32] = {1}, key_b[32] = {2};
    CHECK(ap_packet_init(&packet, key_a, key_b) == AP_OK);
    CHECK(ap_packet_init(&remote, key_b, key_a) == AP_OK);
    size_t total = 0;
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(ap_packet_send(&remote, (uint8_t)i, "xyz", 3) == AP_OK);
        memcpy(wire + total, remote.tx, remote.tx_bytes); total += remote.tx_bytes;
        CHECK(ap_packet_advance(&remote, remote.tx_bytes) == AP_OK);
    }
    size_t used;
    CHECK(ap_packet_feed(&packet, wire, total, &used) == AP_OK && used == 10);
    CHECK(ap_packet_consume(&packet) == AP_OK);
    CHECK(ap_packet_feed(&packet, wire + used, total - used, &used) == AP_OK && used == 10);
    ApPacketView view; CHECK(ap_packet_peek(&packet, &view) == AP_OK && view.command == 1);
    CHECK(packet.rx_cipher.next_nonce == 2 && packet.tx_cipher.next_nonce == 0);
    ap_packet_clear(&packet); ap_packet_clear(&remote);
}

int main(void)
{
    known_frames(); tampering_and_limits(); coalesced_and_fragmented();
    puts("AP frames: independent vectors, every-byte fragments, MAC quarantine, partial TX and nonce limits passed");
    return 0;
}
