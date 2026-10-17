#include "protocol/crypto_internal.h"
#include <limits.h>
#include <string.h>

CryptoResult crypto_hash_spans(Crypto *crypto, LPCWSTR algorithm, bool hmac,
                               const void *key, size_t key_size, const CryptoSpan *spans,
                               size_t count, uint8_t *digest, size_t digest_size)
{
    _Alignas(16) uint8_t object[CRYPTO_OBJECT_MAX] = {0};
    BCRYPT_ALG_HANDLE provider = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    ULONG object_size = 0, got = 0;
    if (digest) SecureZeroMemory(digest, digest_size);
    if (!crypto || !digest || (!key && key_size) || (!spans && count)) return CRYPTO_INVALID_ARGUMENT;
    if (key_size > ULONG_MAX) return CRYPTO_LIMIT;
    for (size_t i = 0; i < count; ++i) {
        if (!spans[i].bytes && spans[i].size) return CRYPTO_INVALID_ARGUMENT;
        if (spans[i].size > ULONG_MAX) return CRYPTO_LIMIT;
    }
    CryptoResult result = crypto_open(crypto, algorithm, hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0, &provider);
    if (result != CRYPTO_OK) goto cleanup;
    if (CRYPTO_CALL(crypto, BCryptGetProperty(provider, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size,
                                             sizeof(object_size), &got, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    if (got != sizeof(object_size) || object_size > sizeof(object)) { result = CRYPTO_LIMIT; goto cleanup; }
    if (crypto->live_handles >= CRYPTO_MAX_HANDLES) { result = CRYPTO_LIMIT; goto cleanup; }
    if (CRYPTO_CALL(crypto, BCryptCreateHash(provider, &hash, object, object_size,
                                            (PUCHAR)key, (ULONG)key_size, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    crypto_acquired(crypto);
    for (size_t i = 0; i < count; ++i) {
        if (spans[i].size && CRYPTO_CALL(crypto, BCryptHashData(hash, (PUCHAR)spans[i].bytes, (ULONG)spans[i].size, 0)) < 0) {
            result = CRYPTO_PROVIDER_ERROR; goto cleanup;
        }
    }
    if (CRYPTO_CALL(crypto, BCryptFinishHash(hash, digest, (ULONG)digest_size, 0)) < 0)
        result = CRYPTO_PROVIDER_ERROR;
cleanup:
    crypto_destroy_hash(crypto, &hash);
    crypto_close(crypto, &provider);
    SecureZeroMemory(object, sizeof(object));
    result = crypto_complete(crypto, result);
    if (result != CRYPTO_OK) SecureZeroMemory(digest, digest_size);
    return result;
}

CryptoResult crypto_sha1(Crypto *crypto, const void *bytes, size_t size, uint8_t digest[20])
{
    CryptoSpan span = {bytes, size};
    return crypto_hash_spans(crypto, BCRYPT_SHA1_ALGORITHM, false, NULL, 0, &span, 1, digest, 20);
}

CryptoResult crypto_sha256(Crypto *crypto, const void *bytes, size_t size, uint8_t digest[32])
{
    CryptoSpan span = {bytes, size};
    return crypto_hash_spans(crypto, BCRYPT_SHA256_ALGORITHM, false, NULL, 0, &span, 1, digest, 32);
}

CryptoResult crypto_hmac_sha1(Crypto *crypto, const void *key, size_t key_size,
                             const void *bytes, size_t size, uint8_t digest[20])
{
    CryptoSpan span = {bytes, size};
    return crypto_hash_spans(crypto, BCRYPT_SHA1_ALGORITHM, true, key, key_size, &span, 1, digest, 20);
}

CryptoResult crypto_ap_derive_keys(Crypto *crypto, const void *secret, size_t secret_size,
                                  const void *transcript, size_t size, CryptoSessionKeys *keys)
{
    uint8_t material[100] = {0};
    if (keys) SecureZeroMemory(keys, sizeof(*keys));
    if (!crypto || !keys || !secret || !secret_size || (!transcript && size)) return CRYPTO_INVALID_ARGUMENT;
    if (secret_size > 96 || size > 131072) return CRYPTO_LIMIT;
    if (((const uint8_t *)secret)[0] == 0) return CRYPTO_INVALID_ARGUMENT;
    CryptoResult result = CRYPTO_OK;
    for (uint8_t i = 1; i <= 5; ++i) {
        CryptoSpan spans[] = {{transcript, size}, {&i, 1}};
        result = crypto_hash_spans(crypto, BCRYPT_SHA1_ALGORITHM, true, secret, secret_size,
                                   spans, 2, material + (size_t)(i - 1) * 20, 20);
        if (result != CRYPTO_OK) goto cleanup;
    }
    result = crypto_hmac_sha1(crypto, material, 20, transcript, size, keys->challenge);
    if (result == CRYPTO_OK) {
        memcpy(keys->tx, material + 20, 32); memcpy(keys->rx, material + 52, 32);
    }
cleanup:
    SecureZeroMemory(material, sizeof(material));
    if (result != CRYPTO_OK) SecureZeroMemory(keys, sizeof(*keys));
    return result;
}
