#include "decrypt_test.h"
#include <string.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static size_t heap_calls;
static int heap_hook(int kind, void *data, size_t size, int use, long request, const unsigned char *file, int line)
{
    (void)data; (void)size; (void)use; (void)request; (void)file; (void)line;
    if (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC) { ++heap_calls; }
    return 1;
}
#endif

DecryptTest dt;
uint8_t dt_cipher[DT_FILE_BYTES], dt_carry16[96], dt_carry32[96];
static const uint8_t norm[16] = {0,0,0xc0,0xc0,0,0,0x40,0x3f,0,0,0x40,0xc0,0,0,0xa0,0x3f};

uint8_t dt_plain(uint64_t offset)
{
    if (offset >= 144 && offset < 160) { return norm[(size_t)(offset - 144)]; }
    if (offset >= 167 && offset < 172) { return ((const uint8_t *)"OggS\0")[(size_t)(offset - 167)]; }
    if (offset < 172) { return 0; }
    return (uint8_t)((offset * 29 + 17) ^ (offset >> 8));
}
void dt_check_zero(const void *data, size_t bytes)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; ++i) { CHECK(p[i] == 0); }
}
void dt_reset(void)
{
    CHECK(dt.stream.active == NULL);
    memset(&dt, 0, sizeof(dt));
    dt.media.active_slot = -1; dt.ranges.active = -1;
    dt.source.key = (MediaKeyHandle){1,0,0}; dt.source.track.format = 2;
    dt.media.slots[0].id = dt.source.key;
    dt.media.slots[0].source = dt.source;
    dt.media.slots[0].stage = MS_READY; dt.media.slots[0].notified = true;
    for (size_t i = 0; i < 16; ++i) { dt.media.slots[0].secret[i] = (uint8_t)i; }
    RangeSource id = {1,0,0};
    dt.ranges.sources[0] = (RangeSourceRecord){.id = id, .live = true, .size_known = true, .total = DT_FILE_BYTES};
    dt.ranges.pool = dt.pool;
    memcpy(dt.pool, dt_cipher, sizeof(dt_cipher));
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) {
        dt.ranges.blocks[i].owner = &dt.ranges;
        if (i < 3) {
            dt.ranges.blocks[i].source = id; dt.ranges.blocks[i].state = RANGE_READY;
            dt.ranges.blocks[i].offset = i * RANGE_BLOCK_BYTES;
            dt.ranges.blocks[i].bytes = i == 2 ? 333 : RANGE_BLOCK_BYTES;
        }
    }
    CHECK(decrypt_stream_init(&dt.stream, &dt.media, &dt.ranges, &dt.source, id) == DECRYPT_OK);
}
void dt_read(uint64_t logical, size_t bytes)
{
    DecryptOperation op = {0}; size_t got = 99; SpotifyHeader h;
    Crypto crypto; crypto_init(&crypto);
    CHECK(decrypt_stream_prepare(&dt.stream, logical, bytes, &op) == DECRYPT_OK);
    CHECK(dt.media.slots[0].leases == 1 && op.key.ticket != 0);
    memset(dt.scratch, 0xa5, sizeof(dt.scratch)); memset(dt.output, 0xa5, sizeof(dt.output));
    CHECK(decrypt_read_at(&op, &crypto, dt.scratch, sizeof(dt.scratch), dt.output + 1, bytes, &got, &h) == DECRYPT_OK);
    size_t expected = DT_FILE_BYTES - 167 - (size_t)logical;
    if (expected > bytes) { expected = bytes; }
    CHECK(got == expected && h.track_gain_db == -6 && h.album_peak == 1.25f);
    for (size_t i = 0; i < got; ++i) { CHECK(dt.output[1 + i] == dt_plain(logical + 167 + i)); }
    CHECK(dt.output[0] == 0xa5 && dt.output[got + 1] == 0xa5);
    dt_check_zero(dt.scratch, sizeof(dt.scratch));
    CHECK(crypto_balanced(&crypto) && crypto.peak_handles == 2);
    CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_OK);
    dt_check_zero(&op, sizeof(op)); CHECK(dt.media.slots[0].leases == 0);
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) { CHECK(dt.ranges.blocks[i].references == 0); }
    CHECK(memcmp(dt.pool, dt_cipher, sizeof(dt_cipher)) == 0);
}
static void continuous_and_fragments(void)
{
    dt_reset();
    dt_read(0, 65536); CHECK(memcmp(dt.output + 1, "OggS", 4) == 0);
    static uint8_t continuous[65536]; memcpy(continuous, dt.output + 1, sizeof(continuous));
    static const size_t starts[] = {0,1,15,16,17,166,167,1576,1577,1578,65360,65369,65370,65371};
    for (size_t i = 0; i < sizeof(starts)/sizeof(starts[0]); ++i) { dt_read(starts[i], 257); }
    dt_read(0, 65536); CHECK(memcmp(dt.output + 1, continuous, sizeof(continuous)) == 0);
    uint32_t random = 0x15;
    size_t position = 0;
    while (position < sizeof(continuous)) {
        random = random * 1664525u + 1013904223u;
        size_t amount = 1 + random % 997;
        if (amount > sizeof(continuous) - position) { amount = sizeof(continuous) - position; }
        dt_read(position, amount);
        CHECK(memcmp(dt.output + 1, continuous + position, amount) == 0); position += amount;
    }
    for (size_t i = 0; i < 64; ++i) {
        random = random * 1664525u + 1013904223u;
        dt_read(random % (DT_FILE_BYTES - 167), 2000);
    }
    dt_read(DT_FILE_BYTES - 168, 65536); /* exact one-byte tail */
    dt_read(DT_FILE_BYTES - 167, 0);
    CHECK(decrypt_stream_retire(&dt.stream) == DECRYPT_OK);
}
static void carry_windows(void)
{
    const uint64_t offsets[] = {(UINT64_C(65536) - 0x0d93) * 16 - 32,
        (UINT64_C(4294967296) - 0x3f630d93) * 16 - 32};
    const uint8_t *windows[] = {dt_carry16, dt_carry32};
    for (size_t n = 0; n < 2; ++n) {
        dt_reset(); uint64_t offset = offsets[n], block = offset - offset % RANGE_BLOCK_BYTES;
        RangeBlock *b = &dt.ranges.blocks[1]; b->offset = block; b->bytes = RANGE_BLOCK_BYTES;
        dt.ranges.blocks[2].state = RANGE_EMPTY;
        dt.ranges.sources[0].total = block + RANGE_BLOCK_BYTES;
        memcpy(dt.pool + RANGE_BLOCK_BYTES + (size_t)(offset - block), windows[n], 96);
        uint8_t continuous[96];
        for (size_t split = 0; split < 3; ++split) {
            size_t inside = split == 0 ? 0 : split == 1 ? 31 : 33;
            DecryptOperation op = {0}; Crypto crypto; crypto_init(&crypto); size_t got;
            CHECK(decrypt_stream_prepare(&dt.stream, offset - 167 + inside, 96 - inside, &op) == DECRYPT_OK);
            CHECK(decrypt_read_at(&op, &crypto, dt.scratch, sizeof(dt.scratch), dt.output, 96, &got, NULL) == DECRYPT_OK);
            CHECK(got == 96 - inside && crypto_balanced(&crypto));
            for (size_t i = 0; i < got; ++i) { CHECK(dt.output[i] == dt_plain(offset + inside + i)); }
            if (split == 0) { memcpy(continuous, dt.output, 96); }
            else { CHECK(memcmp(continuous + inside, dt.output, got) == 0); }
            CHECK(memcmp(dt.pool + RANGE_BLOCK_BYTES + (size_t)(offset - block), windows[n], 96) == 0);
            CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_OK);
        }
    }
}
static void load(const char *name, void *data, size_t bytes)
{
    char path[1024]; CHECK(sprintf_s(path, sizeof(path), "%s/%s", DECRYPT_FIXTURE_DIR, name) > 0);
    FILE *file = NULL; CHECK(fopen_s(&file, path, "rb") == 0 && file != NULL);
    CHECK(fread(data, 1, bytes, file) == bytes && fgetc(file) == EOF && fclose(file) == 0);
}
int main(void)
{
    load("continuous.bin", dt_cipher, sizeof(dt_cipher));
    load("carry16.bin", dt_carry16, sizeof(dt_carry16)); load("carry32.bin", dt_carry32, sizeof(dt_carry32));
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(heap_hook); size_t before = heap_calls;
#endif
    continuous_and_fragments(); carry_windows(); dt_error_tests(); dt_lifecycle_tests();
#if defined(_MSC_VER) && defined(_DEBUG)
    CHECK(heap_calls == before); _CrtSetAllocHook(NULL);
#endif
    puts("audio_ctr_stream passed (independent fixtures, immutable ciphertext, lease quiescence)"); return 0;
}
