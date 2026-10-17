/* Pure-C Shannon port, derived from shannon 0.2.0 (MIT, Paul Lietar).
 * See vendor/licenses/shannon/LICENSE and docs/specs/shannon.md. */
#ifndef SPOTIC_SHANNON_H
#define SPOTIC_SHANNON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SHANNON_KEY_MAX = 32, SHANNON_NONCE_MAX = 16, SHANNON_MAC_MAX = 32 };

typedef struct ShannonCtx {
    uint32_t r[16], crc[16], initial[16];
    uint32_t konst, sbuf, mbuf;
    unsigned nbuf;
    unsigned mode;
    bool keyed, active;
} ShannonCtx;

/* Caller-owned, single-direction state; no allocations. Buffers must not overlap
 * the context. Invalid calls leave it unchanged. Key lengths 1..32, nonce lengths
 * 0..16, MAC lengths 1..32. A nonce is required after keying and after finish.
 * Encrypt/decrypt may be fragmented at any byte, but cannot mix within a nonce.
 * NULL data is valid only for zero length. Decrypted bytes are UNAUTHENTICATED
 * until the caller compares the MAC. Prefer shannon_adapter for AP use. */
bool shannon_key(ShannonCtx *ctx, const void *key, size_t size);
bool shannon_nonce(ShannonCtx *ctx, const void *nonce, size_t size);
bool shannon_encrypt(ShannonCtx *ctx, void *data, size_t size);
bool shannon_decrypt(ShannonCtx *ctx, void *data, size_t size);
bool shannon_finish(ShannonCtx *ctx, void *mac, size_t size);
/* Volatile byte stores erase the entire object, including padding. */
void shannon_clear(ShannonCtx *ctx);

#endif
