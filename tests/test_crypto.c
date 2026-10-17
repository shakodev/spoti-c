#include "crypto_test.h"
#include "crypto_vectors.h"

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static size_t heap_calls;
static int __cdecl heap_hook(int type, void *data, size_t size, int block,
                             long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block; (void)request; (void)file; (void)line;
    ++heap_calls;
    return 1;
}
#endif

static void hashes(const CryptoFixtures *f)
{
    Crypto c; crypto_init(&c);
    uint8_t digest[32];
    for (size_t i = 0; i < f->hashes_count; ++i) {
        const CryptoHashesVector *v = &f->hashes[i];
        CHECK(crypto_sha1(&c, v->data, v->data_size, digest) == CRYPTO_OK);
        CHECK(memcmp(digest, v->sha1, 20) == 0);
        CHECK(crypto_sha256(&c, v->data, v->data_size, digest) == CRYPTO_OK);
        CHECK(memcmp(digest, v->sha256, 32) == 0);
        CHECK(crypto_balanced(&c));
    }
    for (size_t i = 0; i < f->hmac_count; ++i) {
        const CryptoHmacVector *v = &f->hmac[i];
        CHECK(crypto_hmac_sha1(&c, v->key, v->key_size, v->data, v->data_size, digest) == CRYPTO_OK);
        CHECK(memcmp(digest, v->digest, 20) == 0);
    }
    CHECK(crypto_sha1(&c, NULL, 0, digest) == CRYPTO_OK);
    CHECK(memcmp(digest, f->hashes[0].sha1, 20) == 0);
    CHECK(crypto_hmac_sha1(&c, NULL, 0, NULL, 0, digest) == CRYPTO_OK);
    CHECK(memcmp(digest, f->hmac[f->hmac_count - 1].digest, 20) == 0);
    memset(digest, 0xa5, sizeof(digest));
    CHECK(crypto_sha256(&c, NULL, 1, digest) == CRYPTO_INVALID_ARGUMENT);
    CHECK(crypto_test_zero(digest, 32));
    CHECK(crypto_sha1(&c, "abc", SIZE_MAX, digest) == CRYPTO_LIMIT);
    CHECK(crypto_sha1(NULL, NULL, 0, digest) == CRYPTO_INVALID_ARGUMENT);
    CHECK(crypto_sha256(&c, NULL, 0, NULL) == CRYPTO_INVALID_ARGUMENT);
    CHECK(crypto_balanced(&c));
}

static void transcripts(const CryptoFixtures *f)
{
    Crypto c; crypto_init(&c);
    for (size_t i = 0; i < f->transcripts_count; ++i) {
        const CryptoTranscriptsVector *v = &f->transcripts[i];
        CryptoSessionKeys keys;
        CHECK(crypto_ap_derive_keys(&c, v->secret, v->secret_size, v->transcript, v->transcript_size, &keys) == CRYPTO_OK);
        CHECK(memcmp(keys.challenge, v->challenge, 20) == 0);
        CHECK(memcmp(keys.tx, v->tx, 32) == 0 && memcmp(keys.rx, v->rx, 32) == 0);
        uint8_t data[2048], digest[20];
        CHECK(v->transcript_size + 1 <= sizeof(data));
        memcpy(data, v->transcript, v->transcript_size);
        for (uint8_t j = 1; j <= 5; ++j) {
            data[v->transcript_size] = j;
            CHECK(crypto_hmac_sha1(&c, v->secret, v->secret_size, data, v->transcript_size + 1, digest) == CRYPTO_OK);
            CHECK(memcmp(digest, v->material + (size_t)(j - 1) * 20, 20) == 0);
        }
        SecureZeroMemory(&keys, sizeof(keys)); SecureZeroMemory(digest, sizeof(digest));
        CHECK(crypto_balanced(&c));
    }
    uint8_t padded[96] = {0};
    CryptoSessionKeys rejected;
    memset(&rejected, 0xa5, sizeof(rejected));
    CHECK(crypto_ap_derive_keys(&c, padded, sizeof(padded), "wire", 4, &rejected) == CRYPTO_INVALID_ARGUMENT);
    CHECK(crypto_test_zero(&rejected, sizeof(rejected)));
    CHECK(crypto_ap_derive_keys(&c, padded, sizeof(padded) + 1, NULL, 0, &rejected) == CRYPTO_LIMIT);
    CHECK(crypto_ap_derive_keys(&c, f->transcripts[0].secret, f->transcripts[0].secret_size,
                                 "wire", 131073, &rejected) == CRYPTO_LIMIT);
}

static void ctr(const CryptoFixtures *f)
{
    Crypto c; crypto_init(&c);
    uint8_t buffer[2080];
    static const size_t splits[] = {0,1,15,16,17,166,167,1023,1024,1025};
    for (size_t i = 0; i < f->ctr_count; ++i) {
        const CryptoCtrVector *v = &f->ctr[i];
        size_t size = v->plaintext_size;
        CHECK(size <= 2071);
        for (size_t offset = 0; offset < 4; ++offset) {
            memset(buffer, 0xa5, sizeof(buffer));
            memcpy(buffer + offset, v->plaintext, size);
            CHECK(crypto_ctr_xor_at(&c, v->key, v->iv, v->offset, buffer + offset, size) == CRYPTO_OK);
            CHECK(memcmp(buffer + offset, v->ciphertext, size) == 0);
            CHECK(buffer[offset + size] == 0xa5);
            for (size_t k = 0; k < offset; ++k) CHECK(buffer[k] == 0xa5);
            CHECK(crypto_ctr_xor_at(&c, v->key, v->iv, v->offset, buffer + offset, size) == CRYPTO_OK);
            CHECK(memcmp(buffer + offset, v->plaintext, size) == 0);
        }
        for (size_t j = 0; j < sizeof(splits) / sizeof(splits[0]); ++j) {
            size_t split = splits[j] < size ? splits[j] : size;
            memcpy(buffer, v->plaintext, size);
            CHECK(crypto_ctr_xor_at(&c, v->key, v->iv, v->offset, buffer, split) == CRYPTO_OK);
            if (split < size)
                CHECK(crypto_ctr_xor_at(&c, v->key, v->iv, v->offset + split, buffer + split, size - split) == CRYPTO_OK);
            CHECK(memcmp(buffer, v->ciphertext, size) == 0);
        }
        CHECK(crypto_balanced(&c));
    }
    uint8_t key[16] = {0}, iv[16]; memset(iv, 0xff, sizeof(iv));
    memset(buffer, 0xa5, sizeof(buffer));
    CHECK(crypto_ctr_xor_at(&c, key, iv, 16, buffer, 1) == CRYPTO_LIMIT);
    CHECK(crypto_ctr_xor_at(&c, key, iv, 15, buffer, 2) == CRYPTO_LIMIT);
    CHECK(crypto_ctr_xor_at(&c, key, iv, UINT64_MAX, buffer, 2) == CRYPTO_LIMIT);
    CHECK(crypto_ctr_xor_at(&c, key, iv, 0, buffer, SIZE_MAX) == CRYPTO_LIMIT);
    CHECK(buffer[0] == 0xa5 && buffer[1] == 0xa5);
    CHECK(crypto_ctr_xor_at(&c, key, iv, UINT64_MAX, NULL, 0) == CRYPTO_OK);
    CHECK(crypto_ctr_xor_at(&c, NULL, iv, 0, buffer, 1) == CRYPTO_INVALID_ARGUMENT);
    CHECK(crypto_balanced(&c));
}

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
int main(void)
{
    CryptoFixtures fixtures = {cv_dh, COUNT(cv_dh), cv_hashes, COUNT(cv_hashes), cv_hmac, COUNT(cv_hmac),
        cv_transcripts, COUNT(cv_transcripts), cv_ctr, COUNT(cv_ctr), cv_ap_gs, cv_ap_signature,
        sizeof(cv_ap_gs), sizeof(cv_ap_signature)};
    CHECK(memcmp(cv_prime, crypto_dh_prime, 96) == 0);
    CHECK(memcmp(cv_modulus, crypto_ap_modulus, 256) == 0);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(heap_hook);
    size_t before = heap_calls;
#endif
    hashes(&fixtures); transcripts(&fixtures); ctr(&fixtures);
    test_crypto_dh(&fixtures); test_crypto_faults(&fixtures);
#if defined(_MSC_VER) && defined(_DEBUG)
    CHECK(heap_calls == before);
    _CrtSetAllocHook(NULL);
#endif
    puts("protocol_crypto passed (CNG custom DH, signed AP, hashes/HMAC, CTR, provider faults; no owned leaks)");
    return 0;
}
