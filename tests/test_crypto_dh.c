#include "crypto_test.h"

void crypto_test_import(CryptoDh *dh, Crypto *crypto, const CryptoDhVector *v)
{
    /* Deterministic private keys exist ONLY in test fixtures. Production init
     * always asks CNG to generate fresh random private material. */
    struct { BCRYPT_DH_KEY_BLOB header; uint8_t p[96], g[96], y[96], x[96]; } blob = {0};
    CHECK(v->private_size == 96 && v->public_size <= 96);
    blob.header.dwMagic = BCRYPT_DH_PRIVATE_MAGIC; blob.header.cbKey = 96;
    memcpy(blob.p, crypto_dh_prime, 96); blob.g[95] = 2;
    memcpy(blob.y + 96 - v->public_size, v->public, v->public_size);
    memcpy(blob.x, v->private, 96);
    dh->owner = crypto;
    CHECK(crypto_open(crypto, BCRYPT_DH_ALGORITHM, 0, &dh->algorithm) == CRYPTO_OK);
    CHECK(BCryptImportKeyPair(dh->algorithm, NULL, BCRYPT_DH_PRIVATE_BLOB, &dh->key,
                              (PUCHAR)&blob, sizeof(blob), 0) >= 0);
    crypto_acquired(crypto);
    SecureZeroMemory(&blob, sizeof(blob));
}

static void signed_ap(const CryptoFixtures *f)
{
    Crypto c; crypto_init(&c);
    CHECK(crypto_ap_verify_signature(&c, f->gs, f->gs_size, f->signature, f->signature_size) == CRYPTO_OK);
    uint8_t signature[256], peer[96];
    for (size_t i = 0; i < sizeof(signature); ++i) {
        memcpy(signature, f->signature, sizeof(signature)); signature[i] ^= 1;
        CHECK(crypto_ap_verify_signature(&c, f->gs, f->gs_size, signature, sizeof(signature)) == CRYPTO_AUTH_FAILED);
        CHECK(crypto_balanced(&c));
    }
    for (size_t i = 0; i < sizeof(peer); ++i) {
        memcpy(peer, f->gs, sizeof(peer)); peer[i] ^= 1;
        CHECK(crypto_ap_verify_signature(&c, peer, sizeof(peer), f->signature, f->signature_size) != CRYPTO_OK);
    }
    CHECK(crypto_ap_verify_signature(&c, f->gs, f->gs_size, f->signature, 255) == CRYPTO_AUTH_FAILED);
    CHECK(crypto_ap_verify_signature(&c, f->gs, f->gs_size, f->signature, 257) == CRYPTO_AUTH_FAILED);
    CHECK(crypto_ap_verify_signature(&c, f->gs, f->gs_size, NULL, 0) == CRYPTO_AUTH_FAILED);
    CHECK(crypto_balanced(&c));
}

static void known_dh(const CryptoFixtures *f)
{
    Crypto c; crypto_init(&c);
    for (size_t i = 0; i < f->dh_count; ++i) {
        const CryptoDhVector *v = &f->dh[i];
        CryptoDh dh = {0};
        crypto_test_import(&dh, &c, v);
        uint8_t output[96]; size_t size = 0;
        CHECK(crypto_dh_export_public(&dh, output, &size) == CRYPTO_OK);
        CHECK(size == v->public_size && memcmp(output, v->public, size) == 0);
        CHECK(crypto_test_zero(output + size, 96 - size));
        CHECK(crypto_dh_derive_raw(&dh, v->peer, v->peer_size, output, &size) == CRYPTO_OK);
        CHECK(size == v->secret_size && memcmp(output, v->secret, size) == 0);
        CHECK(crypto_test_zero(output + size, 96 - size));
        if (i == f->dh_count - 1) {
            CHECK(crypto_dh_derive(&dh, f->gs, f->gs_size, f->signature, f->signature_size, output, &size) == CRYPTO_OK);
            CHECK(size == v->secret_size && memcmp(output, v->secret, size) == 0);
            uint8_t bad[256]; memcpy(bad, f->signature, sizeof(bad)); bad[0] ^= 1;
            CHECK(crypto_dh_derive(&dh, f->gs, f->gs_size, bad, sizeof(bad), output, &size) == CRYPTO_AUTH_FAILED);
            CHECK(size == 0 && crypto_test_zero(output, sizeof(output)));
        }
        static const uint8_t zero = 0, one = 1;
        CHECK(crypto_dh_derive_raw(&dh, &zero, 1, output, &size) == CRYPTO_INVALID_PEER);
        CHECK(crypto_dh_derive_raw(&dh, &one, 1, output, &size) == CRYPTO_INVALID_PEER);
        CHECK(crypto_dh_derive_raw(&dh, NULL, 0, output, &size) == CRYPTO_INVALID_PEER);
        CHECK(crypto_dh_derive_raw(&dh, v->peer, 97, output, &size) == CRYPTO_INVALID_PEER);
        uint8_t upper[96]; memcpy(upper, crypto_dh_prime, 96);
        CHECK(crypto_dh_derive_raw(&dh, upper, 96, output, &size) == CRYPTO_INVALID_PEER);
        --upper[95];
        CHECK(crypto_dh_derive_raw(&dh, upper, 96, output, &size) == CRYPTO_INVALID_PEER);
        memset(upper, 0xff, sizeof(upper));
        CHECK(crypto_dh_derive_raw(&dh, upper, 96, output, &size) == CRYPTO_INVALID_PEER);
        CHECK(size == 0 && crypto_test_zero(output, sizeof(output)));
        CHECK(c.live_handles == 2);
        CHECK(crypto_dh_clear(&dh) == CRYPTO_OK);
        CHECK(crypto_test_zero(&dh, sizeof(dh)) && crypto_balanced(&c));
    }
}

static void provider_group(void)
{
    Crypto c; crypto_init(&c);
    CryptoDh a = {0}, b = {0};
    uint8_t pa[96], pb[96], sa[96], sb[96], random[32];
    size_t pa_size, pb_size, sa_size, sb_size;
    CHECK(crypto_random(&c, random, sizeof(random)) == CRYPTO_OK);
    CHECK(!crypto_test_zero(random, sizeof(random)));
    CHECK(crypto_dh_init(&a, &c) == CRYPTO_OK && crypto_dh_init(&b, &c) == CRYPTO_OK);
    CHECK(crypto_dh_init(&a, &c) == CRYPTO_BAD_STATE);
    CHECK(crypto_dh_export_public(&a, pa, &pa_size) == CRYPTO_OK);
    CHECK(crypto_dh_export_public(&b, pb, &pb_size) == CRYPTO_OK);
    CHECK(crypto_dh_derive_raw(&a, pb, pb_size, sa, &sa_size) == CRYPTO_OK);
    CHECK(crypto_dh_derive_raw(&b, pa, pa_size, sb, &sb_size) == CRYPTO_OK);
    CHECK(sa_size == sb_size && memcmp(sa, sb, sa_size) == 0);
    SecureZeroMemory(sa, sizeof(sa)); SecureZeroMemory(sb, sizeof(sb)); SecureZeroMemory(random, sizeof(random));
    CHECK(crypto_dh_clear(&a) == CRYPTO_OK && crypto_dh_clear(&b) == CRYPTO_OK);
    CHECK(crypto_dh_clear(&a) == CRYPTO_OK && crypto_balanced(&c));
}

void test_crypto_dh(const CryptoFixtures *f)
{
    signed_ap(f); known_dh(f); provider_group();
}
