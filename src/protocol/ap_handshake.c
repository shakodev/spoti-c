#include "ap_handshake.h"
#include "proto_codec.h"
#include "keyexchange.pb.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

static void put_size(uint8_t *out, size_t size)
{
    out[0] = (uint8_t)(size >> 24); out[1] = (uint8_t)(size >> 16);
    out[2] = (uint8_t)(size >> 8); out[3] = (uint8_t)size;
}

void ap_handshake_clear(ApHandshake *handshake)
{
    if (handshake != NULL) { SecureZeroMemory(handshake, sizeof(*handshake)); }
}

ApResult ap_handshake_begin(ApHandshake *h, CryptoDh *dh, const uint8_t nonce[16], uint64_t version)
{
    if (h == NULL || dh == NULL || dh->owner == NULL || nonce == NULL || version == 0) { return AP_INVALID; }
    if (h->started) { return AP_BAD_STATE; }
    ap_handshake_clear(h);
    ClientHello hello = ClientHello_init_zero;
    size_t public_bytes = 0, encoded = 0;
    hello.build_info.product = Product_PRODUCT_CLIENT;
    hello.build_info.product_flags_count = 1;
    hello.build_info.product_flags[0] = ProductFlags_PRODUCT_FLAG_NONE;
    hello.build_info.platform = Platform_PLATFORM_WIN32_X86_64;
    hello.build_info.version = version;
    hello.cryptosuites_supported_count = 1;
    hello.cryptosuites_supported[0] = Cryptosuite_CRYPTO_SUITE_SHANNON;
    hello.login_crypto_hello.has_diffie_hellman = true;
    LoginCryptoDiffieHellmanHello *diffie = &hello.login_crypto_hello.diffie_hellman;
    if (crypto_dh_export_public(dh, diffie->gc.bytes, &public_bytes) != CRYPTO_OK) { return AP_IO_ERROR; }
    diffie->gc.size = (pb_size_t)public_bytes; diffie->server_keys_known = 1;
    hello.client_nonce.size = 16; memcpy(hello.client_nonce.bytes, nonce, 16);
    hello.has_padding = true; hello.padding.size = 1; hello.padding.bytes[0] = 0x1e;
    if (proto_encode(ClientHello_fields, &hello, h->transcript + 6, AP_HELLO_MAX - 6, &encoded) != PROTO_OK) {
        ap_handshake_clear(h); return AP_LIMIT;
    }
    h->hello_bytes = encoded + 6; h->transcript[0] = 0; h->transcript[1] = 4;
    put_size(h->transcript + 2, h->hello_bytes);
    h->dh = dh; h->started = true; h->expected = 4; return AP_OK;
}

static ApResult verify(ApHandshake *h, CryptoSessionKeys *keys)
{
    APResponseMessage response = APResponseMessage_init_zero;
    if (proto_decode(APResponseMessage_fields, h->transcript + h->hello_bytes + 4,
        h->received - 4, &response) != PROTO_OK) { return AP_MALFORMED; }
    if (response.has_upgrade) { return AP_UNSUPPORTED; }
    if (response.has_login_failed) {
        return response.login_failed.error_code == ErrorCode_TryAnotherAP ? AP_RECONNECT : AP_AUTH_FAILED;
    }
    if (!response.has_challenge || !response.challenge.login_crypto_challenge.has_diffie_hellman) {
        return AP_MALFORMED;
    }
    APChallenge *challenge = &response.challenge;
    LoginCryptoDiffieHellmanChallenge *remote = &challenge->login_crypto_challenge.diffie_hellman;
    if (remote->server_signature_key != 0 || !challenge->crypto_challenge.has_shannon ||
        challenge->crypto_challenge.has_rc4_sha1_hmac || challenge->pow_challenge.has_hash_cash ||
        challenge->fingerprint_challenge.has_grain || challenge->fingerprint_challenge.has_hmac_ripemd) {
        return AP_UNSUPPORTED;
    }
    if (remote->gs.size == 0 || remote->gs.size > 96 || remote->gs_signature.size != 256 ||
        challenge->server_nonce.size != 16) { return AP_MALFORMED; }
    uint8_t secret[96]; size_t secret_bytes = 0;
    CryptoResult result = crypto_dh_derive(h->dh, remote->gs.bytes, remote->gs.size,
        remote->gs_signature.bytes, remote->gs_signature.size, secret, &secret_bytes);
    if (result == CRYPTO_OK) {
        result = crypto_ap_derive_keys(h->dh->owner, secret, secret_bytes,
            h->transcript, h->hello_bytes + h->received, keys);
    }
    SecureZeroMemory(secret, sizeof(secret));
    if (result != CRYPTO_OK) {
        return result == CRYPTO_AUTH_FAILED ? AP_BAD_SIGNATURE :
            result == CRYPTO_INVALID_PEER ? AP_MALFORMED : AP_IO_ERROR;
    }
    ClientResponsePlaintext reply = ClientResponsePlaintext_init_zero;
    reply.login_crypto_response.has_diffie_hellman = true;
    reply.login_crypto_response.diffie_hellman.hmac.size = 20;
    memcpy(reply.login_crypto_response.diffie_hellman.hmac.bytes, keys->challenge, 20);
    size_t encoded;
    if (proto_encode(ClientResponsePlaintext_fields, &reply, h->reply + 4, sizeof(h->reply) - 4, &encoded) != PROTO_OK) {
        SecureZeroMemory(&reply, sizeof(reply)); return AP_LIMIT;
    }
    h->reply_bytes = encoded + 4; put_size(h->reply, h->reply_bytes);
    SecureZeroMemory(&reply, sizeof(reply)); return AP_OK;
}

ApResult ap_handshake_feed(ApHandshake *h, const void *bytes, size_t size,
    size_t *consumed, CryptoSessionKeys *keys)
{
    if (consumed == NULL || keys == NULL) { return AP_INVALID; }
    *consumed = 0; SecureZeroMemory(keys, sizeof(*keys));
    if (h == NULL || (bytes == NULL && size != 0)) { return AP_INVALID; }
    if (h->failure != AP_OK) { return h->failure; }
    if (!h->started || h->complete) { return AP_BAD_STATE; }
    const uint8_t *source = bytes;
    while (*consumed < size && h->received < h->expected) {
        size_t take = h->expected - h->received;
        if (take > size - *consumed) { take = size - *consumed; }
        memcpy(h->transcript + h->hello_bytes + h->received, source + *consumed, take);
        h->received += take; *consumed += take;
        if (h->received == 4 && h->expected == 4) {
            const uint8_t *p = h->transcript + h->hello_bytes;
            uint32_t total = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
            if (total < 4 || total > AP_HANDSHAKE_MAX) { h->failure = AP_LIMIT; return h->failure; }
            h->expected = total;
        }
    }
    if (h->received != h->expected) { return AP_PENDING; }
    ApResult result = verify(h, keys);
    if (result != AP_OK) { SecureZeroMemory(keys, sizeof(*keys)); h->failure = result; }
    else { h->complete = true; }
    return result;
}
