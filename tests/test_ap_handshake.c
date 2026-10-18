#include "ap_test.h"
#include "ap_vectors.h"

static ApHandshake handshake;

static void replay(void)
{
    Crypto crypto; crypto_init(&crypto);
    CryptoDh dh = {0}; ap_test_import(&dh, &crypto, av_private, av_public);
    CHECK(ap_handshake_begin(&handshake, &dh, av_nonce, 123456789) == AP_OK);
    CHECK(handshake.hello_bytes == sizeof(av_hello) && memcmp(handshake.transcript, av_hello, sizeof(av_hello)) == 0);
    CHECK(ap_handshake_begin(&handshake, &dh, av_nonce, 123456789) == AP_BAD_STATE);
    CryptoSessionKeys keys;
    for (size_t i = 0; i < sizeof(av_response); ++i) {
        size_t used;
        ApResult result = ap_handshake_feed(&handshake, av_response + i, 1, &used, &keys);
        CHECK(used == 1 && result == (i + 1 == sizeof(av_response) ? AP_OK : AP_PENDING));
        if (result == AP_PENDING) { CHECK(ap_test_zero(&keys, sizeof(keys))); }
    }
    CHECK(handshake.complete && handshake.hello_bytes + handshake.received == sizeof(av_transcript));
    CHECK(memcmp(handshake.transcript, av_transcript, sizeof(av_transcript)) == 0);
    CHECK(memcmp(keys.challenge, av_challenge, 20) == 0 && memcmp(keys.tx, av_tx, 32) == 0 && memcmp(keys.rx, av_rx, 32) == 0);
    CHECK(handshake.reply_bytes == sizeof(av_reply) && memcmp(handshake.reply, av_reply, sizeof(av_reply)) == 0);
    size_t used; CHECK(ap_handshake_feed(&handshake, NULL, 0, &used, &keys) == AP_BAD_STATE);
    CHECK(crypto_dh_clear(&dh) == CRYPTO_OK && crypto_balanced(&crypto));
    ap_handshake_clear(&handshake); CHECK(ap_test_zero(&handshake, sizeof(handshake)));
}

static bool count_agreements(void *context, const char *operation)
{
    if (strstr(operation, "BCryptSecretAgreement") != NULL) { ++*(unsigned *)context; }
    return true;
}

static void bad_signature(void)
{
    Crypto crypto; crypto_init(&crypto);
    unsigned agreements = 0;
    CryptoControl control = {count_agreements, &agreements}; crypto.control = &control;
    CryptoDh dh = {0}; ap_test_import(&dh, &crypto, av_private, av_public);
    uint8_t wire[sizeof(av_response)]; memcpy(wire, av_response, sizeof(wire));
    /* A byte inside the captured 256-byte RSA signature, not a protobuf tag. */
    wire[200] ^= 1;
    CHECK(ap_handshake_begin(&handshake, &dh, av_nonce, 123456789) == AP_OK);
    size_t used; CryptoSessionKeys keys;
    CHECK(ap_handshake_feed(&handshake, wire, sizeof(wire), &used, &keys) == AP_BAD_SIGNATURE);
    CHECK(agreements == 0 && ap_test_zero(&keys, sizeof(keys)) && handshake.reply_bytes == 0);
    CHECK(ap_handshake_feed(&handshake, wire, 1, &used, &keys) == AP_BAD_SIGNATURE && used == 0);
    CHECK(crypto_dh_clear(&dh) == CRYPTO_OK && crypto_balanced(&crypto)); ap_handshake_clear(&handshake);
}

static void malformed_lengths(void)
{
    Crypto crypto; crypto_init(&crypto);
    CryptoDh dh = {0}; ap_test_import(&dh, &crypto, av_private, av_public);
    const uint32_t lengths[] = {0, 1, 3, 65537, UINT32_MAX, 4};
    for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); ++i) {
        CHECK(ap_handshake_begin(&handshake, &dh, av_nonce, 123456789) == AP_OK);
        uint32_t n = lengths[i]; uint8_t bytes[] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
        size_t used; CryptoSessionKeys keys;
        CHECK(ap_handshake_feed(&handshake, bytes, 4, &used, &keys) == (n == 4 ? AP_MALFORMED : AP_LIMIT));
        CHECK(used == 4 && ap_test_zero(&keys, sizeof(keys)));
        ap_handshake_clear(&handshake);
    }
    CHECK(crypto_dh_clear(&dh) == CRYPTO_OK && crypto_balanced(&crypto));
}

static void maximum_transcript(void)
{
    static uint8_t wire[AP_HANDSHAKE_MAX];
    memcpy(wire, av_response, sizeof(av_response));
    wire[0] = 0; wire[1] = 1; wire[2] = wire[3] = 0;
    memcpy(wire + sizeof(av_response), av_large_extra_prefix, sizeof(av_large_extra_prefix));
    memset(wire + sizeof(av_response) + sizeof(av_large_extra_prefix), 'Z',
        sizeof(wire) - sizeof(av_response) - sizeof(av_large_extra_prefix));
    Crypto crypto; crypto_init(&crypto); CryptoDh dh = {0};
    ap_test_import(&dh, &crypto, av_private, av_public);
    CHECK(ap_handshake_begin(&handshake, &dh, av_nonce, 123456789) == AP_OK);
    size_t used; CryptoSessionKeys keys;
    CHECK(ap_handshake_feed(&handshake, wire, sizeof(wire), &used, &keys) == AP_OK && used == sizeof(wire));
    CHECK(handshake.received == AP_HANDSHAKE_MAX);
    CHECK(memcmp(keys.challenge, av_large_challenge, 20) == 0 && memcmp(keys.tx, av_large_tx, 32) == 0 &&
        memcmp(keys.rx, av_large_rx, 32) == 0);
    CHECK(crypto_dh_clear(&dh) == CRYPTO_OK && crypto_balanced(&crypto)); ap_handshake_clear(&handshake);
}

int main(void)
{
    replay(); bad_signature(); malformed_lengths(); maximum_transcript();
    test_ap_session(); test_ap_socket(); test_ap_resolve(); test_ap_http();
    puts("AP handshake: exact independent hello/transcript/HMAC, signed reply, fragments and invalid lengths passed");
    return 0;
}
