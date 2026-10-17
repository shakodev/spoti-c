#include "shannon_test.h"
#include "shannon_vectors.h"

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

static void known_answer(const ShannonVector *v, bool decrypt, size_t first,
                          size_t chunk, size_t offset)
{
    uint8_t buffer[SHANNON_AP_MAX_PLAINTEXT + 8];
    uint8_t key[SHANNON_KEY_MAX + 4], nonce[SHANNON_NONCE_MAX + 4];
    uint8_t mac[SHANNON_MAC_MAX + 2];
    ShannonCtx ctx;
    bool (*process)(ShannonCtx *, void *, size_t) = decrypt ? shannon_decrypt : shannon_encrypt;
    memset(buffer, 0xa5, sizeof(buffer));
    memset(mac, 0x5a, sizeof(mac));
    memset(&ctx, 0xa5, sizeof(ctx));
    memcpy(key + offset, v->key, v->key_size);
    memcpy(nonce + offset, v->nonce, v->nonce_size);
    memcpy(buffer + offset, decrypt ? v->ciphertext : v->plaintext, v->size);
    CHECK(shannon_key(&ctx, key + offset, v->key_size));
    CHECK(shannon_nonce(&ctx, nonce + offset, v->nonce_size));
    ShannonCtx before = ctx;
    CHECK(process(&ctx, NULL, 0));
    CHECK(memcmp(&before, &ctx, sizeof(ctx)) == 0);
    CHECK(first <= v->size && chunk != 0);
    CHECK(process(&ctx, buffer + offset, first));
    for (size_t i = first; i < v->size;) {
        size_t size = v->size - i < chunk ? v->size - i : chunk;
        CHECK(process(&ctx, buffer + offset + i, size));
        i += size;
    }
    CHECK(shannon_finish(&ctx, mac + 1, v->mac_size));
    CHECK(memcmp(mac + 1, v->mac, v->mac_size) == 0);
    CHECK(mac[0] == 0x5a && mac[v->mac_size + 1] == 0x5a);
    CHECK(memcmp(buffer + offset, decrypt ? v->plaintext : v->ciphertext, v->size) == 0);
    for (size_t i = 0; i < offset; ++i) CHECK(buffer[i] == 0xa5);
    for (size_t i = offset + v->size; i < sizeof(buffer); ++i) CHECK(buffer[i] == 0xa5);
    CHECK(!shannon_finish(&ctx, mac, v->mac_size));
    CHECK(!process(&ctx, buffer, 1));
    shannon_clear(&ctx);
    CHECK(all_zero(&ctx, sizeof(ctx)));
}

static void vectors(void)
{
    for (size_t i = 0; i < sizeof(shannon_vectors) / sizeof(shannon_vectors[0]); ++i) {
        const ShannonVector *v = &shannon_vectors[i];
        for (unsigned direction = 0; direction < 2; ++direction) {
            for (size_t offset = 0; offset < 4; ++offset)
                known_answer(v, direction != 0, 0, v->size ? v->size : 1, offset);
            if (v->size <= 33) {
                for (size_t split = 0; split <= v->size; ++split)
                    known_answer(v, direction != 0, split, 100, 1);
            }
            static const size_t chunks[] = {1,2,3,4,5,7,8,9,31,32,33,32767,65535};
            for (size_t j = 0; j < sizeof(chunks) / sizeof(chunks[0]); ++j)
                known_answer(v, direction != 0, 0, chunks[j], 3);
        }
    }
}

static void invalid_calls(void)
{
    ShannonCtx ctx = {0}, before = ctx;
    uint8_t key[32] = {0}, byte = 0x80, mac[4];
    CHECK(!shannon_key(NULL, key, sizeof(key)));
    CHECK(!shannon_nonce(&ctx, NULL, 0));
    CHECK(!shannon_encrypt(&ctx, &byte, 1));
    CHECK(!shannon_finish(&ctx, mac, sizeof(mac)));
    CHECK(!shannon_key(&ctx, key, 0));
    CHECK(!shannon_key(&ctx, key, SIZE_MAX));
    CHECK(memcmp(&before, &ctx, sizeof(ctx)) == 0);
    CHECK(shannon_key(&ctx, key, sizeof(key)));
    CHECK(!shannon_encrypt(&ctx, &byte, 1));
    before = ctx;
    CHECK(!shannon_nonce(&ctx, key, SHANNON_NONCE_MAX + 1));
    CHECK(!shannon_nonce(&ctx, NULL, 1));
    CHECK(memcmp(&before, &ctx, sizeof(ctx)) == 0);
    CHECK(shannon_nonce(&ctx, NULL, 0));
    CHECK(shannon_encrypt(&ctx, &byte, 1));
    before = ctx;
    CHECK(!shannon_decrypt(&ctx, &byte, 1));
    CHECK(!shannon_encrypt(&ctx, NULL, 1));
    CHECK(!shannon_finish(&ctx, mac, 0));
    CHECK(!shannon_finish(&ctx, mac, SHANNON_MAC_MAX + 1));
    CHECK(!shannon_finish(&ctx, NULL, 4));
    CHECK(memcmp(&before, &ctx, sizeof(ctx)) == 0);
    /* Discarding a partial word before a new raw nonce must not retain mbuf. */
    CHECK(shannon_nonce(&ctx, NULL, 0));
    ShannonCtx clean;
    CHECK(shannon_key(&clean, key, sizeof(key)));
    CHECK(shannon_nonce(&clean, NULL, 0));
    CHECK(memcmp(&ctx, &clean, sizeof(ctx)) == 0);
    shannon_clear(&ctx); shannon_clear(&clean); shannon_clear(NULL);
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(heap_hook);
    size_t before = heap_calls;
#endif
    vectors(); invalid_calls();
    test_shannon_adapter(shannon_vectors, sizeof(shannon_vectors) / sizeof(shannon_vectors[0]));
#if defined(_MSC_VER) && defined(_DEBUG)
    CHECK(heap_calls == before);
    _CrtSetAllocHook(NULL);
#endif
    puts("protocol_shannon passed (reference vectors, fragmentation, tamper; no heap allocations)");
    return 0;
}
