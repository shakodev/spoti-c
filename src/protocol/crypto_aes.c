#include "protocol/crypto_internal.h"
#include <string.h>

static bool counter_add(uint8_t counter[16], uint64_t amount)
{
    for (unsigned i = 16; i > 0; --i) {
        unsigned sum = counter[i - 1] + (unsigned)(amount & 255);
        counter[i - 1] = (uint8_t)sum;
        amount = (amount >> 8) + (sum >> 8);
    }
    return amount == 0;
}

CryptoResult crypto_ctr_xor_at(Crypto *crypto, const uint8_t key[16], const uint8_t iv[16],
                              uint64_t file_offset, void *bytes, size_t size)
{
    _Alignas(16) uint8_t object[CRYPTO_OBJECT_MAX] = {0};
    uint8_t counter[16] = {0}, last[16] = {0}, counters[1024] = {0}, stream[1024] = {0};
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_KEY_HANDLE aes = NULL;
    ULONG object_size = 0, got = 0;
    if (!crypto || !key || !iv || (!bytes && size)) return CRYPTO_INVALID_ARGUMENT;
    if (size > PTRDIFF_MAX || (size && (uint64_t)(size - 1) > UINT64_MAX - file_offset)) return CRYPTO_LIMIT;
    if (!size) return CRYPTO_OK;
    size_t skip = (size_t)(file_offset % 16);
    memcpy(counter, iv, 16);
    if (!counter_add(counter, file_offset / 16)) return CRYPTO_LIMIT;
    memcpy(last, counter, 16);
    if (!counter_add(last, (uint64_t)((size - 1 + skip) / 16))) return CRYPTO_LIMIT;
    CryptoResult result = crypto_open(crypto, BCRYPT_AES_ALGORITHM, 0, &algorithm);
    if (result != CRYPTO_OK) goto cleanup;
    if (CRYPTO_CALL(crypto, BCryptSetProperty(algorithm, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                                             sizeof(BCRYPT_CHAIN_MODE_ECB), 0)) < 0 ||
        CRYPTO_CALL(crypto, BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&object_size,
                                             sizeof(object_size), &got, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    if (got != sizeof(object_size) || object_size > sizeof(object)) { result = CRYPTO_LIMIT; goto cleanup; }
    if (crypto->live_handles >= CRYPTO_MAX_HANDLES) { result = CRYPTO_LIMIT; goto cleanup; }
    if (CRYPTO_CALL(crypto, BCryptGenerateSymmetricKey(algorithm, &aes, object, object_size, (PUCHAR)key, 16, 0)) < 0) {
        result = CRYPTO_PROVIDER_ERROR; goto cleanup;
    }
    crypto_acquired(crypto);
    uint8_t *data = bytes;
    size_t position = 0;
    while (position < size) {
        size_t available = size - position;
        size_t blocks = available > sizeof(counters) - skip ? sizeof(counters) / 16 : (available + skip + 15) / 16;
        for (size_t i = 0; i < blocks; ++i) {
            memcpy(counters + i * 16, counter, 16);
            (void)counter_add(counter, 1); /* Last increment may wrap after the final legal block. */
        }
        ULONG count = (ULONG)(blocks * 16);
        if (CRYPTO_CALL(crypto, BCryptEncrypt(aes, counters, count, NULL, NULL, 0, stream, count, &got, 0)) < 0 || got != count) {
            result = CRYPTO_PROVIDER_ERROR; goto cleanup;
        }
        size_t amount = available < count - skip ? available : count - skip;
        for (size_t i = 0; i < amount; ++i) data[position + i] ^= stream[skip + i];
        position += amount;
        skip = 0;
    }
cleanup:
    crypto_destroy_key(crypto, &aes); crypto_close(crypto, &algorithm);
    SecureZeroMemory(object, sizeof(object)); SecureZeroMemory(counter, sizeof(counter));
    SecureZeroMemory(last, sizeof(last)); SecureZeroMemory(counters, sizeof(counters)); SecureZeroMemory(stream, sizeof(stream));
    result = crypto_complete(crypto, result);
    if (result == CRYPTO_PROVIDER_ERROR) SecureZeroMemory(bytes, size);
    return result;
}
