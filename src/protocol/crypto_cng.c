#include "protocol/crypto_internal.h"
#include <limits.h>

void crypto_init(Crypto *crypto)
{
    if (crypto) SecureZeroMemory(crypto, sizeof(*crypto));
}

bool crypto_balanced(const Crypto *crypto)
{
    return crypto && crypto->live_handles == 0 && !crypto->cleanup_failed;
}

bool crypto_permit(Crypto *crypto, const char *operation)
{
    return !crypto->control || !crypto->control->permit ||
           crypto->control->permit(crypto->control->context, operation);
}

void crypto_acquired(Crypto *crypto)
{
    ++crypto->live_handles;
    if (crypto->live_handles > crypto->peak_handles) crypto->peak_handles = crypto->live_handles;
}

CryptoResult crypto_open(Crypto *crypto, LPCWSTR name, ULONG flags, BCRYPT_ALG_HANDLE *algorithm)
{
    if (crypto->live_handles >= CRYPTO_MAX_HANDLES) return CRYPTO_LIMIT;
    NTSTATUS status = CRYPTO_CALL(crypto, BCryptOpenAlgorithmProvider(algorithm, name, MS_PRIMITIVE_PROVIDER, flags));
    if (status < 0) return CRYPTO_PROVIDER_ERROR;
    crypto_acquired(crypto);
    return CRYPTO_OK;
}

#define RELEASE(function, type, extra) \
    void crypto_##function(Crypto *crypto, type *handle) { \
        if (*handle) { \
            NTSTATUS status = extra; \
            if (status < 0) crypto->cleanup_failed = true; \
            else { *handle = NULL; --crypto->live_handles; } \
        } \
    }
RELEASE(close, BCRYPT_ALG_HANDLE, BCryptCloseAlgorithmProvider(*handle, 0))
RELEASE(destroy_key, BCRYPT_KEY_HANDLE, BCryptDestroyKey(*handle))
RELEASE(destroy_hash, BCRYPT_HASH_HANDLE, BCryptDestroyHash(*handle))
RELEASE(destroy_secret, BCRYPT_SECRET_HANDLE, BCryptDestroySecret(*handle))
#undef RELEASE

CryptoResult crypto_complete(Crypto *crypto, CryptoResult result)
{
    return crypto->cleanup_failed ? CRYPTO_PROVIDER_ERROR : result;
}

CryptoResult crypto_random(Crypto *crypto, void *bytes, size_t size)
{
    if (!crypto || (!bytes && size)) return CRYPTO_INVALID_ARGUMENT;
    if (size > ULONG_MAX) return CRYPTO_LIMIT;
    if (!size) return CRYPTO_OK;
    NTSTATUS status = CRYPTO_CALL(crypto, BCryptGenRandom(NULL, bytes, (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    if (status < 0) {
        SecureZeroMemory(bytes, size);
        return CRYPTO_PROVIDER_ERROR;
    }
    return CRYPTO_OK;
}
