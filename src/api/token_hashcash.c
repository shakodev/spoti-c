#include "token_internal.h"
#include <string.h>

static uint64_t read_be(const uint8_t *p)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) { value = (value << 8) | p[i]; } return value;
}
static void write_be(uint8_t *p, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) { p[7 - i] = (uint8_t)(value >> (i * 8)); }
}
TokenResult token_hash_begin(TokenManager *m, const void *prefix, size_t bytes, int32_t difficulty, bool login)
{
    if (bytes == 0 || bytes > sizeof(m->prefix) || difficulty < 0 || difficulty > TOKEN_DIFFICULTY) { return TOKEN_CHALLENGE_LIMIT; }
    if (prefix != m->prefix) { memcpy(m->prefix, prefix, bytes); }
    m->prefix_bytes = bytes; m->difficulty = (unsigned)difficulty;
    Crypto crypto; crypto_init(&crypto); uint8_t digest[20];
    CryptoResult result = crypto_sha1(&crypto, m->context, m->context_bytes, digest);
    if (result == CRYPTO_OK) { m->hash_seed = read_be(digest + 12); }
    SecureZeroMemory(digest, sizeof(digest));
    if (result != CRYPTO_OK || !crypto_balanced(&crypto)) { return TOKEN_HTTP_ERROR; }
    m->hash_counter = 0; m->hash_started = m->now; m->stage = login ? TS_LOGIN_HASH : TS_CLIENT_HASH;
    return TOKEN_PENDING;
}
TokenResult token_hash_step(TokenManager *m)
{
    uint8_t input[TOKEN_PREFIX + 16], digest[20];
    memcpy(input, m->prefix, m->prefix_bytes);
    Crypto crypto; crypto_init(&crypto); TokenResult result = TOKEN_PENDING; bool solved = false;
    for (unsigned i = 0; i < TOKEN_HASH_SLICE; ++i) {
        result = token_clock(m); if (result != TOKEN_OK) { break; }
        if (m->now - m->hash_started >= m->config.challenge_timeout_ms) { result = TOKEN_TIMEOUT; break; }
        if (m->hash_counter >= m->config.challenge_iterations) { result = TOKEN_CHALLENGE_LIMIT; break; }
        write_be(m->suffix, m->hash_seed + m->hash_counter); write_be(m->suffix + 8, m->hash_counter); ++m->hash_counter;
        memcpy(input + m->prefix_bytes, m->suffix, sizeof(m->suffix));
        if (crypto_sha1(&crypto, input, m->prefix_bytes + 16, digest) != CRYPTO_OK) { result = TOKEN_HTTP_ERROR; break; }
        uint64_t mask = ((uint64_t)1 << m->difficulty) - 1;
        if ((read_be(digest + 12) & mask) == 0) { solved = true; result = TOKEN_OK; break; }
        result = TOKEN_PENDING;
    }
    SecureZeroMemory(input, sizeof(input)); SecureZeroMemory(digest, sizeof(digest));
    if (!crypto_balanced(&crypto)) { return TOKEN_HTTP_ERROR; }
    if (solved) { return m->stage == TS_CLIENT_HASH ? token_client_start(m, true) : token_login_start(m, true); }
    return result;
}
