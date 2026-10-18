#include "decrypt_test.h"
#include <string.h>

static DecryptResult execute(DecryptOperation *op, size_t capacity, size_t output_capacity)
{
    Crypto crypto; crypto_init(&crypto); size_t got = 99;
    memset(dt.scratch, 0xa5, sizeof(dt.scratch)); memset(dt.output, 0xa5, sizeof(dt.output));
    DecryptResult r = decrypt_read_at(op, &crypto, dt.scratch, capacity, dt.output, output_capacity, &got, NULL);
    CHECK(got == 0 && crypto_balanced(&crypto));
    for (size_t i = 0; i < sizeof(dt.output); ++i) { CHECK(dt.output[i] == 0xa5); }
    if (capacity >= 172 && capacity <= sizeof(dt.scratch)) { dt_check_zero(dt.scratch, capacity); }
    CHECK(decrypt_stream_finish(&dt.stream, op) == DECRYPT_OK); return r;
}
static void no_leases(void)
{
    CHECK(dt.media.slots[0].leases == 0 && dt.stream.active == NULL);
    for (size_t i = 0; i < RANGE_LEASES; ++i) { CHECK(dt.ranges.leases[i].value == 0); }
    for (size_t i = 0; i < MEDIA_LEASES; ++i) { CHECK(dt.media.leases[i].ticket == 0); }
    for (size_t i = 0; i < RANGE_BLOCKS; ++i) { CHECK(dt.ranges.blocks[i].references == 0); }
}
typedef struct Fault { size_t calls, fail; } Fault;
static bool permit(void *context, const char *call)
{
    Fault *f = context; CHECK(call != NULL); return ++f->calls != f->fail;
}
static size_t crypto_failure(size_t fail)
{
    dt_reset(); DecryptOperation op = {0}; Crypto crypto; crypto_init(&crypto);
    Fault f = {0, fail}; CryptoControl control = {permit, &f}; crypto.control = &control;
    CHECK(decrypt_stream_prepare(&dt.stream, 65400, 4000, &op) == DECRYPT_OK);
    memset(dt.output, 0xa5, sizeof(dt.output)); size_t got = 99;
    CHECK(decrypt_read_at(&op, &crypto, dt.scratch, sizeof(dt.scratch), dt.output, sizeof(dt.output), &got, NULL) ==
        (fail == 0 ? DECRYPT_OK : DECRYPT_CRYPTO));
    if (fail != 0) {
        CHECK(got == 0);
        for (size_t i = 0; i < sizeof(dt.output); ++i) { CHECK(dt.output[i] == 0xa5); }
    }
    CHECK(crypto_balanced(&crypto)); dt_check_zero(dt.scratch, sizeof(dt.scratch));
    CHECK(memcmp(dt.pool, dt_cipher, sizeof(dt_cipher)) == 0);
    CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_OK); no_leases(); return f.calls;
}
void dt_error_tests(void)
{
    dt_reset(); DecryptOperation op = {0};
    CHECK(decrypt_stream_prepare(&dt.stream, UINT64_MAX, 0, &op) == DECRYPT_LIMIT);
    CHECK(decrypt_stream_prepare(&dt.stream, UINT64_MAX - 167, 1, &op) == DECRYPT_LIMIT);
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 65537, &op) == DECRYPT_LIMIT); no_leases();
    dt.ranges.sources[0].size_known = false;
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_PENDING); no_leases();
    dt.ranges.sources[0].size_known = true;
    for (uint64_t total = 0; total < 172; ++total) {
        dt.ranges.sources[0].total = total;
        CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_PREFIX_TRUNCATED); no_leases();
    }
    dt_reset(); dt.ranges.blocks[0].state = RANGE_EMPTY;
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_PENDING); no_leases();
    dt_reset(); dt.ranges.blocks[1].state = RANGE_EMPTY;
    CHECK(decrypt_stream_prepare(&dt.stream, 65400, 32, &op) == DECRYPT_PENDING); no_leases();
    dt_reset(); dt.ranges.blocks[0].bytes = 166;
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_PREFIX_TRUNCATED); no_leases();
    dt_reset(); dt.ranges.blocks[1].bytes = 1;
    CHECK(decrypt_stream_prepare(&dt.stream, 65400, 32, &op) == DECRYPT_RANGE_ERROR); no_leases();
    dt_reset(); dt.ranges.sources[0].failure = RANGE_HTTP_ERROR;
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_RANGE_ERROR); no_leases();
    dt_reset(); ++dt.stream.source.generation;
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_STALE); no_leases();
    dt_reset(); ++dt.stream.key.generation;
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_STALE); no_leases();
    dt_reset();
    for (size_t i = 0; i < MEDIA_LEASES; ++i) { dt.media.leases[i].ticket = 99; }
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_FULL);
    dt_reset();
    for (size_t i = 0; i < RANGE_LEASES; ++i) { dt.ranges.leases[i].value = 99; }
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_FULL && dt.media.slots[0].leases == 0);
    dt_reset();
    for (size_t i = 0; i < RANGE_LEASES - 1; ++i) { dt.ranges.leases[i].value = 99; }
    CHECK(decrypt_stream_prepare(&dt.stream, 65400, 32, &op) == DECRYPT_FULL);
    CHECK(dt.media.slots[0].leases == 0 && dt.ranges.leases[RANGE_LEASES - 1].value == 0);
    CHECK(dt.ranges.blocks[0].references == 0 && dt.ranges.blocks[1].references == 0);
    dt_reset();
    for (int32_t format = -1; format < 6; ++format) {
        if (format >= 0 && format <= 2) { continue; }
        dt.source.track.format = format;
        CHECK(decrypt_stream_init(&dt.stream, &dt.media, &dt.ranges, &dt.source, (RangeSource){1,0,0}) == DECRYPT_UNSUPPORTED);
    }
    dt_reset();
    for (size_t i = 0; i < 2; ++i) {
        CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_OK);
        CHECK(execute(&op, i == 0 ? 171 : sizeof(dt.scratch), i == 1 ? 31 : sizeof(dt.output)) == DECRYPT_INVALID);
    }
    /* Prefix corruption is detected before exposing even a seeked payload. */
    dt_reset(); dt.pool[167] ^= 1;
    CHECK(decrypt_stream_prepare(&dt.stream, 65400, 32, &op) == DECRYPT_OK);
    CHECK(execute(&op, sizeof(dt.scratch), sizeof(dt.output)) == DECRYPT_PREFIX_CORRUPT);
    dt_reset(); dt.pool[146] ^= 0xc0 ^ 0x80; dt.pool[147] ^= 0xc0 ^ 0x7f; /* -6 -> +infinity */
    CHECK(decrypt_stream_prepare(&dt.stream, 10, 32, &op) == DECRYPT_OK);
    CHECK(execute(&op, sizeof(dt.scratch), sizeof(dt.output)) == DECRYPT_PREFIX_CORRUPT);
    dt_reset();
    for (size_t i = 0; i < 2; ++i) {
        CHECK(decrypt_stream_prepare(&dt.stream, DT_FILE_BYTES - 167 + i, 1, &op) == DECRYPT_OK);
        CHECK(execute(&op, sizeof(dt.scratch), sizeof(dt.output)) == DECRYPT_EOF);
    }
    CHECK(decrypt_stream_prepare(&dt.stream, DT_FILE_BYTES - 166, 0, &op) == DECRYPT_OK);
    CHECK(execute(&op, sizeof(dt.scratch), sizeof(dt.output)) == DECRYPT_EOF);
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_OK);
    DecryptOperation copy = op; Crypto crypto; crypto_init(&crypto); size_t got;
    CHECK(decrypt_read_at(&copy, &crypto, dt.scratch, sizeof(dt.scratch), dt.output, sizeof(dt.output), &got, NULL) == DECRYPT_STALE);
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &copy) == DECRYPT_BAD_STATE);
    CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_OK); /* cancel before dispatch */
    CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_STALE);
    CHECK(decrypt_read_at(&op, &crypto, dt.scratch, sizeof(dt.scratch), dt.output, sizeof(dt.output), &got, NULL) == DECRYPT_STALE); no_leases();
    /* Partial finish can retry without double-releasing already cleared ranges. */
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_OK);
    dt.media.busy = true;
    CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_BAD_STATE);
    CHECK(op.prefix.ticket == 0 && op.payload.ticket == 0 && op.key.ticket != 0);
    dt.media.busy = false;
    CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_OK); no_leases();
    CHECK(decrypt_stream_prepare(&dt.stream, 0, 32, &op) == DECRYPT_OK);
    CHECK(decrypt_read_at(&op, &crypto, dt.scratch, sizeof(dt.scratch), dt.output, sizeof(dt.output), &got, NULL) == DECRYPT_OK);
    CHECK(decrypt_read_at(&op, &crypto, dt.scratch, sizeof(dt.scratch), dt.output, sizeof(dt.output), &got, NULL) == DECRYPT_BAD_STATE);
    CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_OK); no_leases();
    size_t calls = crypto_failure(0);
    for (size_t fail = 1; fail <= calls; ++fail) { (void)crypto_failure(fail); }
}
