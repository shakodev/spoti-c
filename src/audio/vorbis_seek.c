#include "vorbis_internal.h"

AudioResult decoder_seek(VorbisDecoder *d, uint64_t generation, uint64_t target)
{
    if (d == NULL || generation == 0) { return AUDIO_INVALID; }
    if (d->owner_thread != GetCurrentThreadId() || d->busy) { return AUDIO_BAD_STATE; }
    if (generation <= d->config.generation) { return AUDIO_STALE; }
    if (!d->ready || d->config.read_at == NULL) { return AUDIO_BAD_STATE; }
    if (target > UINT64_MAX / d->native.sample_rate) { return AUDIO_LIMIT; }
    uint64_t length = 0;
    if (d->index.final_known) {
        (void)audio_convert_length(d->index.total_frames, d->native.sample_rate, d->config.output.sample_rate, &length);
        if (target > length) { return AUDIO_INVALID; }
    }
    d->config.generation = generation; d->failure = AUDIO_OK; d->target = target;
    vorbis_clear_input(d, 0); d->probes = 0; d->probe_used = d->probe_wanted = 0;
    (void)audio_convert_init(&d->convert, d->native, d->config.output, target);
    /* Flush immediately: acknowledgement retires every old frame/input before
     * any potentially delayed range probing on this new generation. */
    if (d->handle != NULL) { stb_vorbis_flush_pushdata(d->handle); }
    d->seek_state = d->index.final_known ? SEEK_REFINE : SEEK_TAIL;
    return AUDIO_OK;
}
static AudioResult probe(VorbisDecoder *d, uint64_t offset)
{
    if (d->probe_wanted == 0) {
        d->probe_offset = offset; d->probe_used = 0;
        uint64_t left = d->config.source_bytes - offset;
        d->probe_wanted = left < DECODER_WINDOW_BYTES ? (size_t)left : DECODER_WINDOW_BYTES;
    }
    while (d->probe_used < d->probe_wanted) {
        size_t got = 0, capacity = d->probe_wanted - d->probe_used;
        d->busy = true;
        AudioResult r = d->config.read_at(d->config.context, d->config.generation,
            d->probe_offset + d->probe_used, d->window + d->probe_used, capacity, &got);
        d->busy = false;
        if (got > capacity || (r == AUDIO_NEED_MORE && got != 0)) { return AUDIO_CORRUPT; }
        if (r == AUDIO_NEED_MORE) { return r; }
        if (r != AUDIO_OK && r != AUDIO_EOF) { return r; }
        if (got == 0 || (r == AUDIO_EOF && got != capacity)) { return AUDIO_TRUNCATED; }
        d->probe_used += got;
    }
    size_t at = 0;
    while (at < d->probe_used) {
        OggPage page;
        AudioResult r = ogg_page_find(d->window + at, d->probe_used - at, d->probe_offset + at, DECODER_WINDOW_BYTES, &page);
        if (r != AUDIO_OK) { break; }
        r = ogg_index_add(&d->index, &page); if (r != AUDIO_OK) { return r; }
        at = (size_t)(page.offset - d->probe_offset) + page.bytes;
    }
    d->probe_used = d->probe_wanted = 0; ++d->probes; return AUDIO_OK;
}
AudioResult vorbis_seek_drive(VorbisDecoder *d)
{
    if (d->seek_state == SEEK_TAIL) {
        uint64_t start = d->config.source_bytes > DECODER_WINDOW_BYTES ? d->config.source_bytes - DECODER_WINDOW_BYTES : 0;
        AudioResult r = probe(d, start); if (r != AUDIO_OK) { return r; }
        if (!d->index.final_known) { return AUDIO_TRUNCATED; }
        if (d->index.file_bytes!=d->config.source_bytes) {
            if (d->index.file_bytes<d->probe_offset || d->index.file_bytes>d->config.source_bytes) { return AUDIO_CORRUPT; }
            size_t tail=(size_t)(d->index.file_bytes-d->probe_offset);
            if (tail>DECODER_WINDOW_BYTES) { return AUDIO_TRUNCATED; }
            r=vorbis_end_trim(d,d->index.file_bytes,d->window+tail,(size_t)(d->config.source_bytes-d->index.file_bytes));
            if (r!=AUDIO_OK) { return r; }
        }
        d->seek_state = SEEK_REFINE;
    }
    uint64_t length;
    AudioResult r = audio_convert_length(d->index.total_frames, d->native.sample_rate, d->config.output.sample_rate, &length);
    if (r != AUDIO_OK) { return r; }
    if (d->target > length) { return AUDIO_INVALID; }
    if (d->target == length) {
        d->native_next = d->index.total_frames; d->final_seen = d->eof = true;
        d->window_offset = d->validated_offset = d->config.source_bytes; d->used = 0;
        d->seek_state = SEEK_NONE; return AUDIO_EOF;
    }
    uint64_t native = d->target * d->native.sample_rate / d->config.output.sample_rate;
    uint64_t desired = native > 2 * DECODER_FRAME_MAX ? native - 2 * DECODER_FRAME_MAX : 0;
    for (;;) {
        OggPage low, high; ogg_index_bounds(&d->index, desired, &low, &high);
        if (desired == 0 || high.bytes == 0 || high.offset <= low.offset + DECODER_WINDOW_BYTES || d->probes >= 32) {
            return vorbis_restart(d, low.granule == 0 ? 0 : low.offset, low.granule != 0);
        }
        uint64_t middle = low.offset + (high.offset - low.offset) / 2;
        size_t before = d->index.count;
        r = probe(d, middle); if (r != AUDIO_OK) { return r; }
        OggPage next_low, next_high; ogg_index_bounds(&d->index, desired, &next_low, &next_high);
        if (before == d->index.count && next_low.offset == low.offset && next_high.offset == high.offset) {
            /* A packet can span pages, or a midpoint can land near the bound.
             * Fall back to the proven earlier anchor; never estimate PCM. */
            return vorbis_restart(d, low.granule == 0 ? 0 : low.offset, low.granule != 0);
        }
    }
}
