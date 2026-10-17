#ifndef SPOTIC_CRYPTO_TEST_H
#define SPOTIC_CRYPTO_TEST_H
#include "protocol/crypto_internal.h"
#include "support/test_check.h"
#include <string.h>

#define VECTOR_BYTES(name) const uint8_t *name; size_t name##_size
typedef struct CryptoDhVector { VECTOR_BYTES(private); VECTOR_BYTES(public); VECTOR_BYTES(peer); VECTOR_BYTES(secret); } CryptoDhVector;
typedef struct CryptoHashesVector { VECTOR_BYTES(data); VECTOR_BYTES(sha1); VECTOR_BYTES(sha256); } CryptoHashesVector;
typedef struct CryptoHmacVector { VECTOR_BYTES(key); VECTOR_BYTES(data); VECTOR_BYTES(digest); } CryptoHmacVector;
typedef struct CryptoTranscriptsVector { VECTOR_BYTES(secret); VECTOR_BYTES(transcript); VECTOR_BYTES(material);
    VECTOR_BYTES(challenge); VECTOR_BYTES(tx); VECTOR_BYTES(rx); } CryptoTranscriptsVector;
typedef struct CryptoCtrVector { VECTOR_BYTES(key); VECTOR_BYTES(iv); VECTOR_BYTES(plaintext); VECTOR_BYTES(ciphertext); uint64_t offset; } CryptoCtrVector;
#undef VECTOR_BYTES

typedef struct CryptoFixtures {
    const CryptoDhVector *dh; size_t dh_count;
    const CryptoHashesVector *hashes; size_t hashes_count;
    const CryptoHmacVector *hmac; size_t hmac_count;
    const CryptoTranscriptsVector *transcripts; size_t transcripts_count;
    const CryptoCtrVector *ctr; size_t ctr_count;
    const uint8_t *gs, *signature;
    size_t gs_size, signature_size;
} CryptoFixtures;

static inline bool crypto_test_zero(const void *bytes, size_t size)
{
    const uint8_t *data = bytes;
    for (size_t i = 0; i < size; ++i) if (data[i]) return false;
    return true;
}

void crypto_test_import(CryptoDh *dh, Crypto *crypto, const CryptoDhVector *vector);
void test_crypto_dh(const CryptoFixtures *fixtures);
void test_crypto_faults(const CryptoFixtures *fixtures);
#endif
