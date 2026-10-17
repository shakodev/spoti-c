#include "protocol/crypto_internal.h"
#include <string.h>

CryptoResult crypto_ap_verify_signature(Crypto *crypto, const void *gs, size_t gs_size,
                                        const void *signature, size_t signature_size)
{
    struct { BCRYPT_RSAKEY_BLOB header; uint8_t exponent[3], modulus[256]; } blob = {0};
    uint8_t digest[20] = {0}, peer[96] = {0};
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_KEY_HANDLE key = NULL;
    if (!crypto || !gs || (!signature && signature_size)) return CRYPTO_INVALID_ARGUMENT;
    if (!crypto_peer_pad(gs, gs_size, peer)) return CRYPTO_INVALID_PEER;
    if (!signature || signature_size != 256) return CRYPTO_AUTH_FAILED;
    CryptoResult result = crypto_sha1(crypto, gs, gs_size, digest);
    if (result != CRYPTO_OK) goto cleanup;
    result = crypto_open(crypto, BCRYPT_RSA_ALGORITHM, 0, &algorithm);
    if (result != CRYPTO_OK) goto cleanup;
    blob.header.Magic = BCRYPT_RSAPUBLIC_MAGIC; blob.header.BitLength = 2048;
    blob.header.cbPublicExp = 3; blob.header.cbModulus = 256;
    blob.exponent[0] = 1; blob.exponent[2] = 1;
    memcpy(blob.modulus, crypto_ap_modulus, 256);
    if (crypto->live_handles >= CRYPTO_MAX_HANDLES) { result = CRYPTO_LIMIT; goto cleanup; }
    if (CRYPTO_CALL(crypto, BCryptImportKeyPair(algorithm, NULL, BCRYPT_RSAPUBLIC_BLOB,
                                                &key, (PUCHAR)&blob, sizeof(BCRYPT_RSAKEY_BLOB) + 3 + 256, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    crypto_acquired(crypto);
    BCRYPT_PKCS1_PADDING_INFO padding = {BCRYPT_SHA1_ALGORITHM};
    NTSTATUS status = CRYPTO_CALL(crypto, BCryptVerifySignature(key, &padding, digest, sizeof(digest),
                                                                (PUCHAR)signature, (ULONG)signature_size, BCRYPT_PAD_PKCS1));
    if (status < 0) result = status == (NTSTATUS)0xc000a000L ? CRYPTO_AUTH_FAILED : CRYPTO_PROVIDER_ERROR;
cleanup:
    crypto_destroy_key(crypto, &key); crypto_close(crypto, &algorithm);
    SecureZeroMemory(digest, sizeof(digest)); SecureZeroMemory(peer, sizeof(peer));
    SecureZeroMemory(&blob, sizeof(blob));
    return crypto_complete(crypto, result);
}
