#ifndef SPOTIC_PROTOCOL_CRYPTO_INTERNAL_H
#define SPOTIC_PROTOCOL_CRYPTO_INTERNAL_H
#include "protocol/crypto.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

/* Per-owner deterministic fault seam. No globals; cleanup is never intercepted.
 * permit runs before fallible provider work and must not reenter the owner. */
typedef struct CryptoControl { bool (*permit)(void *, const char *); void *context; } CryptoControl;
#define CRYPTO_CALL(c, call) (crypto_permit((c), #call) ? (call) : (NTSTATUS)0xc0000001L)
enum { CRYPTO_OBJECT_MAX = 1024 };
typedef struct CryptoSpan { const void *bytes; size_t size; } CryptoSpan;
extern const uint8_t crypto_dh_prime[96];
extern const uint8_t crypto_ap_modulus[256];
bool crypto_permit(Crypto *crypto, const char *operation);
void crypto_acquired(Crypto *crypto);
CryptoResult crypto_open(Crypto *crypto, LPCWSTR name, ULONG flags, BCRYPT_ALG_HANDLE *algorithm);
void crypto_close(Crypto *crypto, BCRYPT_ALG_HANDLE *algorithm);
void crypto_destroy_key(Crypto *crypto, BCRYPT_KEY_HANDLE *key);
void crypto_destroy_hash(Crypto *crypto, BCRYPT_HASH_HANDLE *hash);
void crypto_destroy_secret(Crypto *crypto, BCRYPT_SECRET_HANDLE *secret);
CryptoResult crypto_complete(Crypto *crypto, CryptoResult result);
CryptoResult crypto_hash_spans(Crypto *crypto, LPCWSTR algorithm, bool hmac,
                               const void *key, size_t key_size, const CryptoSpan *spans,
                               size_t count, uint8_t *digest, size_t digest_size);
bool crypto_peer_pad(const void *peer, size_t size, uint8_t padded[96]);
/* Tests of mathematical byte conventions use this internal primitive with
 * independently known private keys; application callers use verified derive. */
CryptoResult crypto_dh_derive_raw(CryptoDh *dh, const void *peer, size_t peer_size,
                                 uint8_t secret[96], size_t *size);
#endif
