#include "crypto_test.h"

typedef struct Fault { size_t calls, fail; } Fault;
static bool permit(void *context, const char *operation)
{
    Fault *fault = context;
    CHECK(operation != NULL);
    return ++fault->calls != fault->fail;
}

static size_t exercise(const CryptoFixtures *f, unsigned operation, size_t fail_at)
{
    Crypto c; crypto_init(&c);
    CryptoDh dh = {0};
    Fault fault = {0, fail_at};
    CryptoControl control = {permit, &fault};
    uint8_t output[2080]; memset(output, 0xa5, sizeof(output));
    size_t size = 99, output_size = 0;
    CryptoSessionKeys keys;
    memset(&keys, 0xa5, sizeof(keys));
    /* Setup occurs before fault injection and is cleaned up on every path. */
    if (operation >= 7 && operation <= 9) crypto_test_import(&dh, &c, &f->dh[f->dh_count - 1]);
    c.control = &control;
    CryptoResult result;
    switch (operation) {
    case 0: output_size = 20; result = crypto_sha1(&c, "abc", 3, output); break;
    case 1: output_size = 32; result = crypto_sha256(&c, "abc", 3, output); break;
    case 2: output_size = 20; result = crypto_hmac_sha1(&c, "key", 3, "data", 4, output); break;
    case 3: {
        const CryptoTranscriptsVector *v = &f->transcripts[0];
        result = crypto_ap_derive_keys(&c, v->secret, v->secret_size, v->transcript, v->transcript_size, &keys);
        break;
    }
    case 4: {
        const CryptoCtrVector *v = &f->ctr[1];
        output_size = v->plaintext_size;
        result = crypto_ctr_xor_at(&c, v->key, v->iv, v->offset, output, output_size); break;
    }
    case 5: result = crypto_ap_verify_signature(&c, f->gs, f->gs_size, f->signature, f->signature_size); break;
    case 6: result = crypto_dh_init(&dh, &c); break;
    case 7: output_size = 96; result = crypto_dh_export_public(&dh, output, &size); break;
    case 8: output_size = 96; result = crypto_dh_derive_raw(&dh, f->gs, f->gs_size, output, &size); break;
    case 9: output_size = 96; result = crypto_dh_derive(&dh, f->gs, f->gs_size, f->signature, f->signature_size, output, &size); break;
    case 10: output_size = 32; result = crypto_random(&c, output, output_size); break;
    default: result = CRYPTO_BAD_STATE; break;
    }
    CHECK(result == (fail_at ? CRYPTO_PROVIDER_ERROR : CRYPTO_OK));
    if (fail_at) {
        CHECK(fault.calls == fail_at);
        if (output_size) CHECK(crypto_test_zero(output, output_size));
        if (operation == 3) CHECK(crypto_test_zero(&keys, sizeof(keys)));
        if (operation >= 7 && operation <= 9) CHECK(size == 0);
    }
    CHECK(crypto_dh_clear(&dh) == CRYPTO_OK);
    CHECK(crypto_test_zero(&dh, sizeof(dh)));
    CHECK(crypto_balanced(&c) && c.peak_handles <= 4);
    SecureZeroMemory(output, sizeof(output)); SecureZeroMemory(&keys, sizeof(keys));
    return fault.calls;
}

void test_crypto_faults(const CryptoFixtures *f)
{
    for (unsigned operation = 0; operation <= 10; ++operation) {
        size_t calls = exercise(f, operation, 0);
        CHECK(calls != 0);
        for (size_t i = 1; i <= calls; ++i) (void)exercise(f, operation, i);
    }
    Crypto c; crypto_init(&c);
    CryptoDh owned[CRYPTO_MAX_HANDLES / 2] = {0}, extra = {0};
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); ++i) CHECK(crypto_dh_init(&owned[i], &c) == CRYPTO_OK);
    CHECK(c.live_handles == CRYPTO_MAX_HANDLES);
    CHECK(crypto_dh_init(&extra, &c) == CRYPTO_LIMIT);
    uint8_t digest[20];
    CHECK(crypto_sha1(&c, "abc", 3, digest) == CRYPTO_LIMIT);
    CHECK(crypto_test_zero(digest, sizeof(digest)));
    for (size_t i = 0; i < sizeof(owned) / sizeof(owned[0]); ++i) CHECK(crypto_dh_clear(&owned[i]) == CRYPTO_OK);
    CHECK(crypto_balanced(&c));
}
