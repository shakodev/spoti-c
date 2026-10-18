#include "decrypt_stream.h"
#include <string.h>

/* Public protocol constant, corroborated by pinned librespot audio/decrypt.rs
 * (MIT attribution/license retained in proto/LICENSE.librespot). */
static const uint8_t audio_iv[16] = {0x72,0xe0,0x67,0xfb,0xdd,0xcb,0xcf,0x77,0xeb,0xe8,0xbc,0x64,0x3f,0x63,0x0d,0x93};
enum { OP_EMPTY, OP_PREPARED, OP_RUNNING, OP_DONE, OP_FINISHING };

static DecryptResult range_result(RangeResult r)
{
    switch (r) {
    case RANGE_OK: return DECRYPT_OK;
    case RANGE_PENDING: return DECRYPT_PENDING;
    case RANGE_FULL: case RANGE_EXHAUSTED: return DECRYPT_FULL;
    case RANGE_STALE: case RANGE_CANCELLED: case RANGE_CLOSED: return DECRYPT_STALE;
    case RANGE_LIMIT: return DECRYPT_LIMIT;
    default: return DECRYPT_RANGE_ERROR;
    }
}
static DecryptResult key_result(MediaResult r)
{
    if (r == MEDIA_OK) { return DECRYPT_OK; }
    if (r == MEDIA_FULL || r == MEDIA_EXHAUSTED) { return DECRYPT_FULL; }
    if (r == MEDIA_BAD_STATE) { return DECRYPT_BAD_STATE; }
    return DECRYPT_STALE;
}
DecryptResult decrypt_stream_init(DecryptStream *s, MediaResolver *media, RangeFetcher *ranges,
    const MediaSource *source, RangeSource range)
{
    if (s == NULL || media == NULL || ranges == NULL || source == NULL ||
        source->key.generation == 0 || source->key.slot >= MEDIA_SOURCE_SLOTS || source->key.reserved != 0 ||
        range.generation == 0 || range.slot >= RANGE_SOURCES || range.reserved != 0) { return DECRYPT_INVALID; }
    if (s->active != NULL) { return DECRYPT_BAD_STATE; }
    if (source->track.format < 0 || source->track.format > 2) { return DECRYPT_UNSUPPORTED; }
    *s = (DecryptStream){.media = media, .ranges = ranges, .key = source->key,
        .source = range, .format = source->track.format};
    return DECRYPT_OK;
}
DecryptResult decrypt_stream_prepare(DecryptStream *s, uint64_t logical, size_t bytes, DecryptOperation *op)
{
    if (s == NULL || op == NULL || s->media == NULL || s->ranges == NULL) { return DECRYPT_INVALID; }
    if (s->retired) { return DECRYPT_STALE; }
    if (s->active != NULL || op->self != NULL) { return DECRYPT_BAD_STATE; }
    uint64_t encrypted;
    if (bytes > DECRYPT_SCRATCH_MAX || spotify_header_map(logical, bytes, &encrypted) != SPOTIFY_HEADER_OK) { return DECRYPT_LIMIT; }
    uint64_t total;
    DecryptResult r = range_result(range_source_size(s->ranges, s->source, &total));
    if (r != DECRYPT_OK) { return r; }
    if (total < SPOTIFY_OGG_PROBE) { return DECRYPT_PREFIX_TRUNCATED; }
    MediaKeyLease key = {0}; RangeLease prefix = {0}, payload = {0};
    r = key_result(media_key_acquire(s->media, s->key, &key));
    if (r != DECRYPT_OK) { return r; }
    RangeResult acquired = range_acquire(s->ranges, s->source, 0, SPOTIFY_OGG_PROBE, &prefix);
    r = acquired == RANGE_EOF || acquired == RANGE_TRUNCATED ? DECRYPT_PREFIX_TRUNCATED : range_result(acquired);
    size_t take = 0;
    if (r == DECRYPT_OK && encrypted < total) {
        take = total - encrypted < bytes ? (size_t)(total - encrypted) : bytes;
        if (take != 0) { r = range_result(range_acquire(s->ranges, s->source, encrypted, take, &payload)); }
    }
    if (r != DECRYPT_OK) {
        if (prefix.ticket != 0) { (void)range_release(s->ranges, &prefix); }
        (void)media_key_release(s->media, &key); return r;
    }
    op->self = op; op->owner = s; op->key = key; op->prefix = prefix; op->payload = payload;
    op->logical = logical; op->encrypted = encrypted; op->bytes = take; op->format = s->format;
    /* EOF is carried without touching owner state from the decoder. */
    if (encrypted > total || (bytes != 0 && encrypted == total)) { op->logical = UINT64_MAX; }
    atomic_store_explicit(&op->phase, OP_PREPARED, memory_order_release);
    s->active = op; return DECRYPT_OK;
}
static bool copy_lease(const RangeLease *lease, uint64_t offset, size_t bytes, uint8_t *out)
{
    size_t copied = 0;
    for (size_t i = 0; i < lease->count; ++i) {
        const RangeSpan *p = &lease->spans[i];
        if (p->offset != offset + copied || p->bytes > bytes - copied) { return false; }
        memcpy(out + copied, p->data, p->bytes); copied += p->bytes;
    }
    return copied == bytes;
}
DecryptResult decrypt_read_at(DecryptOperation *op, Crypto *crypto, void *scratch, size_t capacity,
    void *output, size_t output_capacity, size_t *read, SpotifyHeader *header)
{
    if (read == NULL) { return DECRYPT_INVALID; } *read = 0;
    if (op == NULL || op->self != op) { return DECRYPT_STALE; }
    unsigned expected = OP_PREPARED;
    if (!atomic_compare_exchange_strong_explicit(&op->phase, &expected, OP_RUNNING, memory_order_acq_rel, memory_order_acquire)) { return DECRYPT_BAD_STATE; }
    DecryptResult r = DECRYPT_INVALID; SpotifyHeader h;
    bool valid_scratch = scratch != NULL && capacity >= SPOTIFY_OGG_PROBE && capacity <= DECRYPT_SCRATCH_MAX;
    if (!valid_scratch || crypto == NULL || capacity < op->bytes || output_capacity < op->bytes ||
        (output == NULL && op->bytes != 0)) { goto done; }
    r = DECRYPT_RANGE_ERROR;
    if (!copy_lease(&op->prefix, 0, SPOTIFY_OGG_PROBE, scratch)) { goto done; }
    r = DECRYPT_CRYPTO;
    if (crypto_ctr_xor_at(crypto, op->key.key, audio_iv, 0, scratch, SPOTIFY_OGG_PROBE) != CRYPTO_OK || !crypto_balanced(crypto)) { goto done; }
    if (spotify_header_parse(op->format, scratch, SPOTIFY_OGG_PROBE, &h) != SPOTIFY_HEADER_OK) { r = DECRYPT_PREFIX_CORRUPT; goto done; }
    if (op->logical == UINT64_MAX) { r = DECRYPT_EOF; goto done; }
    if (op->bytes != 0) {
        r = DECRYPT_RANGE_ERROR;
        if (!copy_lease(&op->payload, op->encrypted, op->bytes, scratch)) { goto done; }
        r = DECRYPT_CRYPTO;
        if (crypto_ctr_xor_at(crypto, op->key.key, audio_iv, op->encrypted, scratch, op->bytes) != CRYPTO_OK || !crypto_balanced(crypto)) { goto done; }
        memcpy(output, scratch, op->bytes);
    }
    if (header != NULL) { *header = h; }
    *read = op->bytes; r = DECRYPT_OK;
done:
    if (valid_scratch) { SecureZeroMemory(scratch, capacity); }
    atomic_store_explicit(&op->phase, OP_DONE, memory_order_release); return r;
}
DecryptResult decrypt_stream_finish(DecryptStream *s, DecryptOperation *op)
{
    if (s == NULL || op == NULL) { return DECRYPT_INVALID; }
    if (s->active != op || op->self != op || op->owner != s) { return DECRYPT_STALE; }
    unsigned phase = atomic_load_explicit(&op->phase, memory_order_acquire);
    for (;;) {
        if (phase == OP_RUNNING) { return DECRYPT_PENDING; }
        if (phase != OP_PREPARED && phase != OP_DONE) { return DECRYPT_BAD_STATE; }
        if (atomic_compare_exchange_strong_explicit(&op->phase, &phase, OP_FINISHING, memory_order_acq_rel, memory_order_acquire)) { break; }
    }
    /* All APIs below run on the owner, which must not reenter dependencies. */
    if ((op->payload.ticket != 0 && range_release(s->ranges, &op->payload) != RANGE_OK) ||
        (op->prefix.ticket != 0 && range_release(s->ranges, &op->prefix) != RANGE_OK) ||
        (op->key.ticket != 0 && media_key_release(s->media, &op->key) != MEDIA_OK)) {
        /* Preserve remaining tickets for an owner retry after a busy dependency.
         * Released leases were cleared by their owning APIs. Never decode again. */
        atomic_store_explicit(&op->phase, OP_DONE, memory_order_release); return DECRYPT_BAD_STATE;
    }
    SecureZeroMemory(op, sizeof(*op)); s->active = NULL; return DECRYPT_OK;
}
DecryptResult decrypt_stream_retire(DecryptStream *s)
{
    if (s == NULL || s->media == NULL) { return DECRYPT_INVALID; }
    s->retired = true; return s->active == NULL ? DECRYPT_OK : DECRYPT_PENDING;
}
