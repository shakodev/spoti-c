#ifndef SPOTIC_AP_TEST_H
#define SPOTIC_AP_TEST_H
#include "protocol/crypto_internal.h"
#include "protocol/ap_handshake.h"
#include "support/test_check.h"
#include <string.h>

/* Test-only CNG import. Production always uses crypto_dh_init/random. */
static inline void ap_test_import(CryptoDh *dh, Crypto *crypto, const uint8_t private_key[96],
    const uint8_t public_key[96])
{
    struct { BCRYPT_DH_KEY_BLOB header; uint8_t p[96], g[96], y[96], x[96]; } blob = {0};
    blob.header.dwMagic = BCRYPT_DH_PRIVATE_MAGIC; blob.header.cbKey = 96;
    memcpy(blob.p, crypto_dh_prime, 96); blob.g[95] = 2;
    memcpy(blob.y, public_key, 96); memcpy(blob.x, private_key, 96);
    dh->owner = crypto;
    CHECK(crypto_open(crypto, BCRYPT_DH_ALGORITHM, 0, &dh->algorithm) == CRYPTO_OK);
    CHECK(BCryptImportKeyPair(dh->algorithm, NULL, BCRYPT_DH_PRIVATE_BLOB, &dh->key,
        (PUCHAR)&blob, sizeof(blob), 0) >= 0);
    crypto_acquired(crypto); SecureZeroMemory(&blob, sizeof(blob));
}
static inline bool ap_test_zero(const void *bytes, size_t size)
{
    const uint8_t *p = bytes;
    for (size_t i = 0; i < size; ++i) { if (p[i] != 0) { return false; } }
    return true;
}
void test_ap_session(void);
void test_ap_socket(void);
void test_ap_resolve(void);
void test_ap_http(void);
#endif
