#include "vorbis_internal.h"
#include <limits.h>

AudioResult decoder_open(Arena *codec, Arena *work, const DecoderConfig *config, VorbisDecoder **out)
{
    if (out == NULL) { return AUDIO_INVALID; } *out = NULL;
    if (codec == NULL || work == NULL || codec == work || config == NULL || config->generation == 0 ||
        config->source_bytes == 0 || config->source_bytes > UINT32_MAX || config->trailing_zero_limit>15 || codec->used != 0 || work->used != 0 ||
        codec->capacity > DECODER_CODEC_BYTES || work->capacity > DECODER_WORK_BYTES) { return AUDIO_INVALID; }
    if (!audio_format_valid(config->output)) { return AUDIO_UNSUPPORTED; }
    ArenaMark cm = arena_mark(codec), wm = arena_mark(work);
    VorbisDecoder *d = arena_alloc(work, sizeof(*d), _Alignof(VorbisDecoder));
    uint8_t *window = arena_alloc(work, DECODER_WINDOW_BYTES, 8);
    float *pcm = arena_alloc(work, (DECODER_FRAME_MAX + 1) * 2 * sizeof(float), _Alignof(float));
    /* Leave all codec memory to stb; its own fixed allocator counts setup/temp. */
    size_t codec_bytes = codec->capacity;
    char *storage = arena_alloc(codec, codec_bytes, 16);
    if (d == NULL || window == NULL || pcm == NULL || storage == NULL) {
        (void)arena_rewind(work, wm); (void)arena_rewind(codec, cm); return AUDIO_NO_MEMORY;
    }
    memset(d, 0, sizeof(*d)); memset(window, 0, DECODER_WINDOW_BYTES);
    memset(pcm, 0, (DECODER_FRAME_MAX + 1) * 2 * sizeof(float));
    d->codec_arena = codec; d->work_arena = work; d->codec_mark = cm; d->work_mark = wm;
    d->config = *config; d->window = window; d->pcm = pcm;
    d->owner_thread = GetCurrentThreadId();
    d->allocation = (stb_vorbis_alloc){storage, (int)codec_bytes};
    *out = d; return AUDIO_OK;
}
AudioResult decoder_input(VorbisDecoder *d, uint64_t generation, uint64_t *offset, size_t *capacity)
{
    if (d == NULL || offset == NULL || capacity == NULL) { return AUDIO_INVALID; }
    if (d->owner_thread != GetCurrentThreadId() || d->busy) { return AUDIO_BAD_STATE; }
    if (generation != d->config.generation) { return AUDIO_STALE; }
    if (d->failure != AUDIO_OK) { return d->failure; }
    if (d->seek_state == SEEK_TAIL || d->seek_state == SEEK_REFINE) { return AUDIO_BAD_STATE; }
    *offset = d->window_offset + d->used;
    *capacity = DECODER_WINDOW_BYTES - d->used;
    if (*capacity > d->config.source_bytes - *offset) { *capacity = (size_t)(d->config.source_bytes - *offset); }
    return d->eof ? AUDIO_EOF : *capacity == 0 ? AUDIO_FULL : AUDIO_OK;
}
AudioResult decoder_push(VorbisDecoder *d, uint64_t generation, uint64_t offset,
    const void *data, size_t bytes, bool eof, size_t *accepted)
{
    if (accepted == NULL) { return AUDIO_INVALID; } *accepted = 0;
    if (d == NULL || (data == NULL && bytes != 0)) { return AUDIO_INVALID; }
    if (d->owner_thread != GetCurrentThreadId() || d->busy) { return AUDIO_BAD_STATE; }
    if (generation != d->config.generation) { return AUDIO_STALE; }
    if (d->failure != AUDIO_OK) { return d->failure; }
    if (d->seek_state == SEEK_TAIL || d->seek_state == SEEK_REFINE) { return AUDIO_BAD_STATE; }
    if (offset != d->window_offset + d->used || bytes > d->config.source_bytes - offset || d->eof) { return AUDIO_INVALID; }
    size_t take = DECODER_WINDOW_BYTES - d->used; if (take > bytes) { take = bytes; }
    if (take != 0) { memcpy(d->window + d->used, data, take); } d->used += take; *accepted = take;
    if (eof && take == bytes) {
        if (offset + bytes != d->config.source_bytes) { d->failure = AUDIO_TRUNCATED; return d->failure; }
        d->eof = true;
    }
    AudioResult r = vorbis_validate(d);
    if (r != AUDIO_OK && r != AUDIO_NEED_MORE) { d->failure = r; return r; }
    return take == bytes ? AUDIO_OK : AUDIO_FULL;
}
AudioResult vorbis_fill(VorbisDecoder *d)
{
    if (d->eof) { return AUDIO_EOF; }
    if (d->config.read_at == NULL) { return AUDIO_NEED_MORE; }
    uint64_t offset; size_t capacity;
    AudioResult r = decoder_input(d, d->config.generation, &offset, &capacity);
    if (r != AUDIO_OK) { return r == AUDIO_FULL ? AUDIO_LIMIT : r; }
    size_t got = 0;
    d->busy = true;
    r = d->config.read_at(d->config.context, d->config.generation, offset, d->window + d->used, capacity, &got);
    d->busy = false;
    if (r == AUDIO_NEED_MORE) { return got == 0 ? r : AUDIO_CORRUPT; }
    if (r != AUDIO_OK && r != AUDIO_EOF) { return r; }
    if (got > capacity || (got == 0 && r != AUDIO_EOF)) { return AUDIO_CORRUPT; }
    /* Already copied into the append location; push's memcpy would overlap. */
    d->used += got;
    d->eof = r == AUDIO_EOF || offset + got == d->config.source_bytes;
    if (d->eof && offset + got != d->config.source_bytes) { return AUDIO_TRUNCATED; }
    r = vorbis_validate(d); return r == AUDIO_NEED_MORE ? AUDIO_OK : r;
}
AudioResult decoder_read_frames(VorbisDecoder *d, uint64_t generation, float *output, size_t capacity, size_t *frames)
{
    if (frames == NULL) { return AUDIO_INVALID; } *frames = 0;
    if (d == NULL || (output == NULL && capacity != 0) || capacity > SIZE_MAX / (2 * sizeof(float))) { return AUDIO_INVALID; }
    if (d->owner_thread != GetCurrentThreadId() || d->busy) { return AUDIO_BAD_STATE; }
    if (generation != d->config.generation) { return AUDIO_STALE; }
    if (d->failure != AUDIO_OK) { return d->failure; }
    if (capacity == 0) { return AUDIO_OK; }
    for (;;) {
        AudioResult r = AUDIO_OK;
        if (d->seek_state == SEEK_TAIL || d->seek_state == SEEK_REFINE) {
            r = vorbis_seek_drive(d); if (r == AUDIO_NEED_MORE) { return r; }
        }
        if (r == AUDIO_OK && d->pcm_frames != 0) {
            size_t count; bool consumed;
            bool final = d->index.final_known && d->pcm_start + d->pcm_frames == d->index.total_frames;
            r = audio_convert_block(&d->convert, d->pcm, d->pcm_frames, d->pcm_start, final,
                output + *frames * d->config.output.channels, capacity - *frames, &count, &consumed);
            *frames += count; if (consumed) { d->pcm_frames = 0; }
            if (r == AUDIO_OK && *frames == capacity) { return AUDIO_OK; }
        }
        if (r == AUDIO_OK && d->pcm_frames == 0) { r = vorbis_pump(d); }
        if (r == AUDIO_NEED_MORE) { r = vorbis_fill(d); if (r == AUDIO_OK) { continue; } }
        if (r == AUDIO_OK) { continue; }
        if (r != AUDIO_NEED_MORE && r != AUDIO_EOF) { d->failure = r; }
        return *frames != 0 ? AUDIO_OK : r;
    }
}
DecoderInfo decoder_info(const VorbisDecoder *d)
{
    if (d == NULL) { return (DecoderInfo){0}; }
    if (d->owner_thread != GetCurrentThreadId()) { return (DecoderInfo){0}; }
    uint64_t length = 0;
    if (d->ready && d->index.final_known) { (void)audio_convert_length(d->index.total_frames, d->native.sample_rate, d->config.output.sample_rate, &length); }
    return (DecoderInfo){d->native, d->config.output, d->config.generation, d->convert.position, length,
        (size_t)d->allocation.alloc_buffer_length_in_bytes, d->setup_peak, d->work_arena->used,
        d->used, d->index.count, d->index.compactions, d->probes, d->ready,
        d->index.final_known, d->seek_state != SEEK_NONE};
}
AudioResult decoder_close(VorbisDecoder *d)
{
    if (d == NULL) { return AUDIO_INVALID; }
    if (d->owner_thread != GetCurrentThreadId() || d->busy) { return AUDIO_BAD_STATE; }
    if (d->handle != NULL) { stb_vorbis_close(d->handle); }
    Arena *codec = d->codec_arena, *work = d->work_arena;
    ArenaMark cm = d->codec_mark, wm = d->work_mark;
    memset(d->allocation.alloc_buffer, 0, (size_t)d->allocation.alloc_buffer_length_in_bytes);
    memset(work->base + wm.used, 0, work->used - wm.used);
    bool a = arena_rewind(codec, cm), b = arena_rewind(work, wm);
    return a && b ? AUDIO_OK : AUDIO_BAD_STATE;
}
