#include "protocol/crypto_internal.h"
#include <string.h>

typedef struct DhParameters { BCRYPT_DH_PARAMETER_HEADER header; uint8_t p[96], g[96]; } DhParameters;
typedef struct DhPublic { BCRYPT_DH_KEY_BLOB header; uint8_t p[96], g[96], y[96]; } DhPublic;

bool crypto_peer_pad(const void *peer, size_t size, uint8_t padded[96])
{
    uint8_t upper[96];
    memset(padded, 0, 96);
    if (!peer || size == 0 || size > 96) return false;
    memcpy(padded + 96 - size, peer, size);
    memcpy(upper, crypto_dh_prime, sizeof(upper));
    --upper[95]; /* The pinned prime ends in ff; upper = p - 1. */
    bool at_least_two = false;
    for (size_t i = 0; i < 95; ++i) at_least_two |= padded[i] != 0;
    at_least_two |= padded[95] >= 2;
    return at_least_two && memcmp(padded, upper, 96) < 0;
}

CryptoResult crypto_dh_clear(CryptoDh *dh)
{
    if (!dh) return CRYPTO_INVALID_ARGUMENT;
    if (!dh->owner) return (dh->key || dh->algorithm) ? CRYPTO_BAD_STATE : CRYPTO_OK;
    Crypto *owner = dh->owner;
    crypto_destroy_key(owner, &dh->key);
    if (!dh->key) crypto_close(owner, &dh->algorithm);
    if (!dh->key && !dh->algorithm) SecureZeroMemory(dh, sizeof(*dh));
    return crypto_complete(owner, CRYPTO_OK);
}

CryptoResult crypto_dh_init(CryptoDh *dh, Crypto *crypto)
{
    DhParameters parameters = {0};
    if (!dh || !crypto) return CRYPTO_INVALID_ARGUMENT;
    if (dh->owner || dh->algorithm || dh->key) return CRYPTO_BAD_STATE;
    dh->owner = crypto;
    CryptoResult result = crypto_open(crypto, BCRYPT_DH_ALGORITHM, 0, &dh->algorithm);
    if (result != CRYPTO_OK) goto cleanup;
    if (crypto->live_handles >= CRYPTO_MAX_HANDLES) { result = CRYPTO_LIMIT; goto cleanup; }
    if (CRYPTO_CALL(crypto, BCryptGenerateKeyPair(dh->algorithm, &dh->key, 768, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    crypto_acquired(crypto);
    parameters.header.cbLength = sizeof(parameters);
    parameters.header.dwMagic = BCRYPT_DH_PARAMETERS_MAGIC;
    parameters.header.cbKeyLength = 96;
    memcpy(parameters.p, crypto_dh_prime, 96); parameters.g[95] = 2;
    if (CRYPTO_CALL(crypto, BCryptSetProperty(dh->key, BCRYPT_DH_PARAMETERS,
                                             (PUCHAR)&parameters, sizeof(parameters), 0)) < 0 ||
        CRYPTO_CALL(crypto, BCryptFinalizeKeyPair(dh->key, 0)) < 0) result = CRYPTO_PROVIDER_ERROR;
cleanup:
    SecureZeroMemory(&parameters, sizeof(parameters));
    if (result != CRYPTO_OK) (void)crypto_dh_clear(dh);
    return crypto_complete(crypto, result);
}

CryptoResult crypto_dh_export_public(CryptoDh *dh, uint8_t public_key[96], size_t *size)
{
    DhPublic blob = {0};
    ULONG got = 0;
    if (public_key) SecureZeroMemory(public_key, 96);
    if (size) *size = 0;
    if (!dh || !public_key || !size) return CRYPTO_INVALID_ARGUMENT;
    if (!dh->owner || !dh->key) return CRYPTO_BAD_STATE;
    CryptoResult result = CRYPTO_OK;
    if (CRYPTO_CALL(dh->owner, BCryptExportKey(dh->key, NULL, BCRYPT_DH_PUBLIC_BLOB,
                                               (PUCHAR)&blob, sizeof(blob), &got, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    if (got != sizeof(blob) || blob.header.dwMagic != BCRYPT_DH_PUBLIC_MAGIC ||
        blob.header.cbKey != 96 || memcmp(blob.p, crypto_dh_prime, 96) != 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    uint8_t generator[96] = {0}, checked[96];
    generator[95] = 2;
    if (memcmp(blob.g, generator, 96) != 0 || !crypto_peer_pad(blob.y, 96, checked)) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    size_t first = 0;
    while (first < 96 && blob.y[first] == 0) ++first;
    if (first == 96) { result = CRYPTO_PROVIDER_ERROR; goto cleanup; }
    *size = 96 - first;
    memcpy(public_key, blob.y + first, *size);
cleanup:
    SecureZeroMemory(&blob, sizeof(blob));
    result = crypto_complete(dh->owner, result);
    if (result != CRYPTO_OK) { SecureZeroMemory(public_key, 96); *size = 0; }
    return result;
}

CryptoResult crypto_dh_derive_raw(CryptoDh *dh, const void *peer, size_t peer_size,
                                 uint8_t secret[96], size_t *size)
{
    DhPublic blob = {0};
    uint8_t raw[96] = {0};
    ULONG got = 0;
    BCRYPT_KEY_HANDLE remote = NULL;
    BCRYPT_SECRET_HANDLE agreement = NULL;
    if (secret) SecureZeroMemory(secret, 96);
    if (size) *size = 0;
    if (!dh || !secret || !size) return CRYPTO_INVALID_ARGUMENT;
    if (!dh->owner || !dh->key) return CRYPTO_BAD_STATE;
    Crypto *crypto = dh->owner;
    CryptoResult result = CRYPTO_INVALID_PEER;
    if (!crypto_peer_pad(peer, peer_size, blob.y)) goto cleanup;
    blob.header.dwMagic = BCRYPT_DH_PUBLIC_MAGIC; blob.header.cbKey = 96;
    memcpy(blob.p, crypto_dh_prime, 96); blob.g[95] = 2;
    if (crypto->live_handles >= CRYPTO_MAX_HANDLES) { result = CRYPTO_LIMIT; goto cleanup; }
    if (CRYPTO_CALL(crypto, BCryptImportKeyPair(dh->algorithm, NULL, BCRYPT_DH_PUBLIC_BLOB,
                                                &remote, (PUCHAR)&blob, sizeof(blob), 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    crypto_acquired(crypto);
    if (crypto->live_handles >= CRYPTO_MAX_HANDLES) { result = CRYPTO_LIMIT; goto cleanup; }
    if (CRYPTO_CALL(crypto, BCryptSecretAgreement(dh->key, remote, &agreement, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    crypto_acquired(crypto);
    if (CRYPTO_CALL(crypto, BCryptDeriveKey(agreement, BCRYPT_KDF_RAW_SECRET, NULL,
                                            raw, sizeof(raw), &got, 0)) < 0 || got != 96) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    size_t length = 96;
    while (length && raw[length - 1] == 0) --length;
    if (length == 0 || (length == 1 && raw[0] <= 1)) { result = CRYPTO_INVALID_PEER; goto cleanup; }
    for (size_t i = 0; i < length; ++i) secret[i] = raw[length - i - 1];
    *size = length;
    result = CRYPTO_OK;
cleanup:
    crypto_destroy_secret(crypto, &agreement);
    crypto_destroy_key(crypto, &remote);
    SecureZeroMemory(raw, sizeof(raw)); SecureZeroMemory(&blob, sizeof(blob));
    result = crypto_complete(crypto, result);
    if (result != CRYPTO_OK) { SecureZeroMemory(secret, 96); *size = 0; }
    return result;
}

CryptoResult crypto_dh_derive(CryptoDh *dh, const void *peer, size_t peer_size,
                             const void *signature, size_t signature_size,
                             uint8_t secret[96], size_t *size)
{
    if (secret) SecureZeroMemory(secret, 96);
    if (size) *size = 0;
    if (!dh || !secret || !size) return CRYPTO_INVALID_ARGUMENT;
    if (!dh->owner || !dh->key) return CRYPTO_BAD_STATE;
    CryptoResult result = crypto_ap_verify_signature(dh->owner, peer, peer_size, signature, signature_size);
    if (result != CRYPTO_OK) return result;
    return crypto_dh_derive_raw(dh, peer, peer_size, secret, size);
}
