#include "decrypt_test.h"
#include <string.h>

typedef struct DecodeWorker {
    HANDLE entered, resume;
    DecryptOperation *op;
    Crypto crypto;
    CryptoControl control;
    DecryptResult result;
    size_t got, calls;
} DecodeWorker;
static bool gate(void *context, const char *call)
{
    DecodeWorker *w = context; CHECK(call != NULL);
    if (++w->calls == 1) {
        CHECK(SetEvent(w->entered)); CHECK(WaitForSingleObject(w->resume, 10000) == WAIT_OBJECT_0);
    }
    return true;
}
static DWORD WINAPI decode(void *context)
{
    DecodeWorker *w = context;
    w->result = decrypt_read_at(w->op, &w->crypto, dt.scratch, sizeof(dt.scratch),
        dt.output, sizeof(dt.output), &w->got, NULL);
    return 0;
}
void dt_lifecycle_tests(void)
{
    for (size_t iteration = 0; iteration < 8; ++iteration) {
        dt_reset(); DecryptOperation op = {0}, other = {0};
        CHECK(decrypt_stream_prepare(&dt.stream, 65360, 1000, &op) == DECRYPT_OK);
        CHECK(dt.media.slots[0].leases == 1);
        const uint8_t *borrowed = op.key.key;
        DecodeWorker w = {0}; w.op = &op;
        crypto_init(&w.crypto); w.control = (CryptoControl){gate, &w}; w.crypto.control = &w.control;
        size_t owned_handles = 0;
        w.entered = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(w.entered != NULL); ++owned_handles;
        w.resume = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(w.resume != NULL); ++owned_handles;
        HANDLE thread = CreateThread(NULL, 0, decode, &w, 0, NULL); CHECK(thread != NULL); ++owned_handles;
        CHECK(WaitForSingleObject(w.entered, 10000) == WAIT_OBJECT_0);
        CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_PENDING);
        CHECK(decrypt_stream_retire(&dt.stream) == DECRYPT_PENDING);
        CHECK(decrypt_stream_prepare(&dt.stream, 0, 1, &other) == DECRYPT_STALE);
        CHECK(media_source_retire(&dt.media, dt.source.key) == MEDIA_OK);
        CHECK(dt.media.slots[0].stage == MS_RETIRED && dt.media.slots[0].leases == 1);
        CHECK(range_cancel_generation(&dt.ranges, dt.stream.source) == RANGE_OK);
        CHECK(dt.ranges.sources[0].live && dt.ranges.sources[0].cancelled);
        CHECK(dt.ranges.blocks[0].references != 0 && dt.ranges.blocks[1].references != 0);
        for (size_t i = 0; i < 16; ++i) { CHECK(borrowed[i] == i); }
        MediaKeyLease key = {0}; RangeLease range = {0};
        CHECK(media_key_acquire(&dt.media, dt.source.key, &key) == MEDIA_STALE);
        CHECK(range_acquire(&dt.ranges, dt.stream.source, 167, 1, &range) == RANGE_STALE);
        CHECK(SetEvent(w.resume)); CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
        CHECK(w.result == DECRYPT_OK && w.got == 1000 && crypto_balanced(&w.crypto));
        for (size_t i = 0; i < w.got; ++i) { CHECK(dt.output[i] == dt_plain(65360 + 167 + i)); }
        dt_check_zero(dt.scratch, sizeof(dt.scratch));
        CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_OK);
        dt_check_zero(borrowed, 16); CHECK(dt.media.slots[0].stage == MS_FREE);
        CHECK(!dt.ranges.sources[0].live);
        for (size_t i = 0; i < RANGE_BLOCKS; ++i) { CHECK(dt.ranges.blocks[i].references == 0); }
        dt_check_zero(dt.pool, sizeof(dt.pool));
        CHECK(decrypt_stream_retire(&dt.stream) == DECRYPT_OK);
        CHECK(decrypt_stream_finish(&dt.stream, &op) == DECRYPT_STALE);
        CHECK(CloseHandle(thread)); --owned_handles;
        CHECK(CloseHandle(w.resume)); --owned_handles;
        CHECK(CloseHandle(w.entered)); --owned_handles; CHECK(owned_handles == 0);
    }
}
