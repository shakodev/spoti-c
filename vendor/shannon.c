/* Pure-C port of the Shannon algorithm in shannon 0.2.0 by Paul Lietar.
 * MIT license: vendor/licenses/shannon/LICENSE.
 * Ported 2026-10-17: fixed storage, checked lifecycle, unsigned byte operations,
 * and MAC accumulation exactly once per completed word across fragmented calls.
 * No Rust/C++ code is compiled into the application. */
#include "shannon.h"
#include <string.h>

#define INITIAL_CONSTANT UINT32_C(0x6996c53a)

static uint32_t rotl(uint32_t value, unsigned count)
{
    return (value << count) | (value >> (32 - count));
}

static uint32_t sbox1(uint32_t value)
{
    value ^= rotl(value, 5) | rotl(value, 7);
    value ^= rotl(value, 19) | rotl(value, 22);
    return value;
}

static uint32_t sbox2(uint32_t value)
{
    value ^= rotl(value, 7) | rotl(value, 22);
    value ^= rotl(value, 5) | rotl(value, 19);
    return value;
}

static uint32_t load32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void store32(uint8_t *data, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) data[i] = (uint8_t)(value >> (8 * i));
}

static void cycle(ShannonCtx *ctx)
{
    uint32_t value = sbox1(ctx->r[12] ^ ctx->r[13] ^ ctx->konst) ^ rotl(ctx->r[0], 1);
    for (unsigned i = 1; i < 16; ++i) ctx->r[i - 1] = ctx->r[i];
    ctx->r[15] = value;
    value = sbox2(ctx->r[2] ^ ctx->r[15]);
    ctx->r[0] ^= value;
    ctx->sbuf = value ^ ctx->r[8] ^ ctx->r[12];
}

static void diffuse(ShannonCtx *ctx)
{
    for (unsigned i = 0; i < 16; ++i) cycle(ctx);
}

static void load_key(ShannonCtx *ctx, const uint8_t *key, size_t size)
{
    size_t i = 0;
    for (; size - i >= 4; i += 4) {
        ctx->r[13] ^= load32(key + i);
        cycle(ctx);
    }
    if (i < size) {
        uint32_t word = 0;
        unsigned shift = 0;
        while (i < size) {
            word |= (uint32_t)key[i++] << shift;
            shift += 8;
        }
        ctx->r[13] ^= word;
        cycle(ctx);
    }
    ctx->r[13] ^= (uint32_t)size;
    cycle(ctx);
    memcpy(ctx->crc, ctx->r, sizeof(ctx->r));
    diffuse(ctx);
    for (unsigned j = 0; j < 16; ++j) ctx->r[j] ^= ctx->crc[j];
}

static void mac_word(ShannonCtx *ctx, uint32_t word)
{
    uint32_t crc = ctx->crc[0] ^ ctx->crc[2] ^ ctx->crc[15] ^ word;
    for (unsigned i = 1; i < 16; ++i) ctx->crc[i - 1] = ctx->crc[i];
    ctx->crc[15] = crc;
    ctx->r[13] ^= word;
}

bool shannon_key(ShannonCtx *ctx, const void *key, size_t size)
{
    if (!ctx || !key || size == 0 || size > SHANNON_KEY_MAX) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->r[0] = ctx->r[1] = 1;
    for (unsigned i = 2; i < 16; ++i) ctx->r[i] = ctx->r[i - 1] + ctx->r[i - 2];
    ctx->konst = INITIAL_CONSTANT;
    load_key(ctx, key, size);
    ctx->konst = ctx->r[0];
    memcpy(ctx->initial, ctx->r, sizeof(ctx->r));
    ctx->keyed = true;
    return true;
}

bool shannon_nonce(ShannonCtx *ctx, const void *nonce, size_t size)
{
    if (!ctx || !ctx->keyed || (!nonce && size) || size > SHANNON_NONCE_MAX) return false;
    memcpy(ctx->r, ctx->initial, sizeof(ctx->r));
    ctx->konst = INITIAL_CONSTANT;
    load_key(ctx, nonce, size);
    ctx->konst = ctx->r[0];
    ctx->nbuf = ctx->mode = 0;
    ctx->mbuf = 0;
    ctx->active = true;
    return true;
}

static void partial(ShannonCtx *ctx, uint8_t *byte, bool decrypt)
{
    unsigned shift = 32 - ctx->nbuf;
    if (decrypt) *byte ^= (uint8_t)(ctx->sbuf >> shift);
    ctx->mbuf ^= (uint32_t)*byte << shift;
    if (!decrypt) *byte ^= (uint8_t)(ctx->sbuf >> shift);
    ctx->nbuf -= 8;
    if (ctx->nbuf == 0) mac_word(ctx, ctx->mbuf);
}

static bool process(ShannonCtx *ctx, void *data, size_t size, bool decrypt)
{
    unsigned mode = decrypt ? 2u : 1u;
    if (!ctx || !ctx->active || (!data && size) || (ctx->mode && ctx->mode != mode)) return false;
    if (!size) return true;
    ctx->mode = mode;
    uint8_t *bytes = data;
    size_t i = 0;
    while (ctx->nbuf && i < size) partial(ctx, &bytes[i++], decrypt);
    for (; size - i >= 4; i += 4) {
        cycle(ctx);
        uint32_t word = load32(bytes + i);
        if (decrypt) word ^= ctx->sbuf;
        mac_word(ctx, word);
        if (!decrypt) word ^= ctx->sbuf;
        store32(bytes + i, word);
    }
    if (i < size) {
        cycle(ctx);
        ctx->mbuf = 0;
        ctx->nbuf = 32;
        while (i < size) partial(ctx, &bytes[i++], decrypt);
    }
    return true;
}

bool shannon_encrypt(ShannonCtx *ctx, void *data, size_t size)
{
    return process(ctx, data, size, false);
}

bool shannon_decrypt(ShannonCtx *ctx, void *data, size_t size)
{
    return process(ctx, data, size, true);
}

bool shannon_finish(ShannonCtx *ctx, void *mac, size_t size)
{
    if (!ctx || !ctx->active || !mac || size == 0 || size > SHANNON_MAC_MAX) return false;
    if (ctx->nbuf) mac_word(ctx, ctx->mbuf);
    cycle(ctx);
    ctx->r[13] ^= INITIAL_CONSTANT ^ ((uint32_t)ctx->nbuf << 3);
    ctx->nbuf = 0;
    for (unsigned i = 0; i < 16; ++i) ctx->r[i] ^= ctx->crc[i];
    diffuse(ctx);
    uint8_t *bytes = mac;
    for (size_t i = 0; i < size;) {
        cycle(ctx);
        for (unsigned j = 0; j < 4 && i < size; ++j, ++i)
            bytes[i] = (uint8_t)(ctx->sbuf >> (8 * j));
    }
    ctx->active = false;
    return true;
}

void shannon_clear(ShannonCtx *ctx)
{
    if (ctx) {
        volatile unsigned char *bytes = (volatile unsigned char *)ctx;
        for (size_t i = 0; i < sizeof(*ctx); ++i) bytes[i] = 0;
    }
}
