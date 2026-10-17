#ifndef SPOTIC_PROTOCOL_CRYPTO_H
#define SPOTIC_PROTOCOL_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { CRYPTO_DH_SIZE = 96, CRYPTO_RSA_SIZE = 256, CRYPTO_SHA1_SIZE = 20,
       CRYPTO_SHA256_SIZE = 32, CRYPTO_AES_SIZE = 16, CRYPTO_MAX_HANDLES = 16 };

typedef enum CryptoResult {
    CRYPTO_OK, CRYPTO_INVALID_ARGUMENT, CRYPTO_LIMIT, CRYPTO_BAD_STATE,
    CRYPTO_INVALID_PEER, CRYPTO_AUTH_FAILED, CRYPTO_PROVIDER_ERROR
} CryptoResult;

struct CryptoControl;
typedef struct Crypto {
    const struct CryptoControl *control; /* Internal provider seam; initialize with crypto_init. */
    size_t live_handles, peak_handles;
    bool cleanup_failed;
} Crypto;

typedef struct CryptoDh {
    Crypto *owner;
    void *algorithm, *key;
} CryptoDh;

typedef struct CryptoSessionKeys { uint8_t challenge[20], tx[32], rx[32]; } CryptoSessionKeys;

/* Caller-owned, single-thread contexts. Initialize Crypto before use, start DH
 * at {0}, clear DH before reusing it, and keep its owner alive until clear.
 * Never copy a live context. At most 16 CNG handles per owner, no application
 * heap allocation. CNG may allocate provider memory internally. Do not invoke
 * on the UI/render or audio callback path. Inputs must not overlap outputs or
 * contexts, except the explicitly in-place CTR byte buffer. */
void crypto_init(Crypto *crypto);
bool crypto_balanced(const Crypto *crypto);
CryptoResult crypto_random(Crypto *crypto, void *bytes, size_t size);
CryptoResult crypto_dh_init(CryptoDh *dh, Crypto *crypto);
CryptoResult crypto_dh_export_public(CryptoDh *dh, uint8_t public_key[96], size_t *size);
/* Verifies the pinned AP signature BEFORE deriving. Both wire gs and signature
 * are exact received bytes. Returns minimal unsigned big-endian secret, with
 * remaining output bytes zero. Failure wipes all 96 output bytes, size = 0. */
CryptoResult crypto_dh_derive(CryptoDh *dh, const void *peer, size_t peer_size,
                             const void *signature, size_t signature_size,
                             uint8_t secret[96], size_t *size);
CryptoResult crypto_dh_clear(CryptoDh *dh);
CryptoResult crypto_ap_verify_signature(Crypto *crypto, const void *gs, size_t gs_size,
                                        const void *signature, size_t signature_size);
CryptoResult crypto_sha1(Crypto *crypto, const void *bytes, size_t size, uint8_t digest[20]);
CryptoResult crypto_sha256(Crypto *crypto, const void *bytes, size_t size, uint8_t digest[32]);
CryptoResult crypto_hmac_sha1(Crypto *crypto, const void *key, size_t key_size,
                             const void *bytes, size_t size, uint8_t digest[20]);
/* Accepts a verified minimal big-endian DH secret (no leading zero), and at most
 * 128 KiB of exact hello/response wire transcript including framing bytes.
 * Hash/key derivation failures wipe the complete output digest/key structure. */
CryptoResult crypto_ap_derive_keys(Crypto *crypto, const void *secret, size_t secret_size,
                                  const void *transcript, size_t size, CryptoSessionKeys *keys);
/* AES-128 ECB(counter) + XOR; 128-bit big-endian counter = iv + offset/16.
 * Offset is the encrypted file offset, including Spotify's 167-byte prefix.
 * Reject byte-offset/counter wrap before mutation. Provider errors wipe the
 * entire byte buffer; discard/refetch it. Invalid arguments/limits leave bytes
 * unchanged. NULL bytes is allowed only for size zero. No block padding. */
CryptoResult crypto_ctr_xor_at(Crypto *crypto, const uint8_t key[16], const uint8_t iv[16],
                              uint64_t file_offset, void *bytes, size_t size);
#endif
