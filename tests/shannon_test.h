#ifndef SPOTIC_SHANNON_TEST_H
#define SPOTIC_SHANNON_TEST_H
#include "protocol/shannon_adapter.h"
#include "support/test_check.h"
#include <string.h>

typedef struct ShannonVector {
    const char *name;
    const uint8_t *key;
    size_t key_size;
    const uint8_t *nonce;
    size_t nonce_size;
    const uint8_t *plaintext, *ciphertext;
    size_t size;
    const uint8_t *mac;
    size_t mac_size;
} ShannonVector;

static inline bool all_zero(const void *memory, size_t size)
{
    const uint8_t *bytes = memory;
    for (size_t i = 0; i < size; ++i) if (bytes[i]) return false;
    return true;
}

static inline uint32_t vector_nonce(const ShannonVector *v)
{
    CHECK(v->nonce_size == 4);
    return ((uint32_t)v->nonce[0] << 24) | ((uint32_t)v->nonce[1] << 16) |
           ((uint32_t)v->nonce[2] << 8) | v->nonce[3];
}

void test_shannon_adapter(const ShannonVector *vectors, size_t count);
#endif
