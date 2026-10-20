#include "vorbis_internal.h"

static uint32_t little32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
AudioResult vorbis_validate(VorbisDecoder *d)
{
    while (d->validated_offset < d->window_offset + d->used) {
        size_t at = (size_t)(d->validated_offset - d->window_offset);
        OggPage page; AudioResult r = ogg_page_parse(d->window + at, d->used - at,
            d->validated_offset, DECODER_WINDOW_BYTES, &page);
        if (r == AUDIO_NEED_MORE) { return d->eof ? AUDIO_TRUNCATED : r; }
        if (r != AUDIO_OK) { return r; }
        if (d->final_seen) { return AUDIO_UNSUPPORTED; }
        if ((page.flags&4) && page.offset+page.bytes<d->config.source_bytes) {
            size_t end=at+page.bytes;
            r=vorbis_end_trim(d,page.offset+page.bytes,d->window+end,d->used-end);
            if (r!=AUDIO_OK) { return r; }
            d->used=end; d->eof=true;
        }
        if (!d->stream_started && page.offset == 0) {
            const uint8_t *p = d->window + at;
            if (page.flags != 2 || page.sequence != 0 || page.granule != 0 || page.bytes != 58 ||
                page.segments != 1 || p[27] != 30 || p[28] != 1 || memcmp(p + 29, "vorbis", 6) != 0) { return AUDIO_UNSUPPORTED; }
            uint32_t rate = little32(p + 40);
            if (little32(p + 35) != 0 || (p[39] != 1 && p[39] != 2) || rate < 8000 || rate > 192000) { return AUDIO_UNSUPPORTED; }
            unsigned small = p[56] & 15, large = p[56] >> 4;
            if (small < 6 || large < small || large > 13 || p[57] != 1) { return AUDIO_CORRUPT; }
        } else if ((page.flags & 2) != 0) { return AUDIO_UNSUPPORTED; }
        if (d->stream_started && (page.sequence != d->sequence + 1 || ((page.flags & 1) != 0) != d->continued)) { return AUDIO_CORRUPT; }
        if (page.granule != UINT64_MAX) {
            if (page.granule < d->previous_granule) { return AUDIO_CORRUPT; }
            d->previous_granule = page.granule;
        }
        const uint8_t *laces = d->window + at + 27;
        const uint8_t *body = laces + page.segments;
        if (!d->stream_started && page.offset != 0) { d->header_packets = 3; d->packet_unknown = (page.flags & 1) != 0; }
        for (size_t i = 0; i < page.segments; ++i) {
            if (d->packet_bytes == 0 && !d->packet_unknown) {
                if (laces[i] == 0) { return AUDIO_CORRUPT; }
                if (d->header_packets < 3) {
                    if (laces[i] < 7 || body[0] != 1 + 2 * d->header_packets || memcmp(body + 1, "vorbis", 6) != 0) { return AUDIO_CORRUPT; }
                } else if ((body[0] & 1) != 0) { return AUDIO_CORRUPT; }
            }
            d->packet_bytes += laces[i]; if (d->packet_bytes > DECODER_WINDOW_BYTES) { return AUDIO_LIMIT; }
            body += laces[i];
            if (laces[i] < 255) {
                d->packet_bytes = 0; d->packet_unknown = false;
                if (d->header_packets < 3) { ++d->header_packets; }
            }
        }
        r = ogg_index_add(&d->index, &page); if (r != AUDIO_OK) { return r; }
        d->stream_started = true; d->sequence = page.sequence; d->continued = page.continued_end;
        d->validated_offset += page.bytes;
        if ((page.flags & 4) != 0) {
            if (d->validated_offset != d->config.source_bytes) { return AUDIO_UNSUPPORTED; }
            d->final_seen = true;
        }
    }
    if (d->eof && !d->final_seen) { return AUDIO_TRUNCATED; }
    return AUDIO_OK;
}
static void consume(VorbisDecoder *d, size_t bytes)
{
    memmove(d->window, d->window + bytes, d->used - bytes); d->used -= bytes; d->window_offset += bytes;
}
static AudioResult need_more(const VorbisDecoder *d)
{
    if (d->eof) { return AUDIO_TRUNCATED; }
    return d->used == DECODER_WINDOW_BYTES ? AUDIO_LIMIT : AUDIO_NEED_MORE;
}
AudioResult vorbis_pump(VorbisDecoder *d)
{
    size_t available = (size_t)(d->validated_offset - d->window_offset);
    if (d->handle == NULL) {
        int consumed = 0, error = VORBIS_outofmem;
        d->handle = stb_vorbis_open_pushdata(d->window, (int)available, &consumed, &error, &d->allocation);
        if (d->handle == NULL) {
            if (error == VORBIS_need_more_data) { return need_more(d); }
            return error == VORBIS_outofmem ? AUDIO_NO_MEMORY : AUDIO_CORRUPT;
        }
        stb_vorbis_info info = stb_vorbis_get_info(d->handle);
        d->native = (AudioFormat){info.sample_rate, (uint32_t)info.channels, AUDIO_FLOAT32};
        if (!audio_format_valid(d->native) || info.max_frame_size > DECODER_FRAME_MAX) { return AUDIO_UNSUPPORTED; }
        size_t temporary = info.setup_temp_memory_required > info.temp_memory_required ? info.setup_temp_memory_required : info.temp_memory_required;
        d->setup_peak = info.setup_memory_required + temporary;
        if (consumed < 0 || (size_t)consumed > available) { return AUDIO_CORRUPT; }
        consume(d, (size_t)consumed); d->ready = true;
        AudioResult r = audio_convert_init(&d->convert, d->native, d->config.output, d->target);
        if (r != AUDIO_OK) { return r; }
        return AUDIO_OK;
    }
    if (d->window_offset == d->config.source_bytes && d->eof) {
        if (!d->final_seen || !d->index.final_known || d->native_next != d->index.total_frames) { return AUDIO_TRUNCATED; }
        d->seek_state = SEEK_NONE; return AUDIO_EOF;
    }
    if (available == 0) { return need_more(d); }
    int channels = 0, samples = 0; float **planar = NULL;
    int consumed = stb_vorbis_decode_frame_pushdata(d->handle, d->window, (int)available, &channels, &planar, &samples);
    int error = stb_vorbis_get_error(d->handle);
    if (error != 0 && error != VORBIS_need_more_data) { return error == VORBIS_outofmem ? AUDIO_NO_MEMORY : AUDIO_CORRUPT; }
    if (consumed < 0 || (size_t)consumed > available || samples < 0 || samples > DECODER_FRAME_MAX) { return AUDIO_CORRUPT; }
    if (consumed == 0) { return need_more(d); }
    consume(d, (size_t)consumed);
    if (samples == 0) { return AUDIO_OK; }
    if (channels != (int)d->native.channels || planar == NULL) { return AUDIO_CORRUPT; }
    if (d->resync && !d->native_known) {
        int endpoint = stb_vorbis_get_sample_offset(d->handle);
        if (endpoint < samples) { return AUDIO_OK; } /* Continue until a known granule. */
        d->native_next = (uint64_t)endpoint - (uint64_t)samples; d->native_known = true;
        uint64_t needed = d->target * d->native.sample_rate / d->config.output.sample_rate;
        if (d->native_next > needed) { return vorbis_restart(d, 0, false); }
    }
    d->pcm_start = d->native_next; d->pcm_frames = (size_t)samples; d->native_next += (uint64_t)samples;
    if (d->index.final_known && d->native_next > d->index.total_frames) { return AUDIO_CORRUPT; }
    for (size_t i = 0; i < (size_t)samples; ++i) {
        for (uint32_t c = 0; c < d->native.channels; ++c) { d->pcm[i * d->native.channels + c] = planar[c][i]; }
    }
    if (d->seek_state == SEEK_DECODE && d->native_next > d->target * d->native.sample_rate / d->config.output.sample_rate) { d->seek_state = SEEK_NONE; }
    return AUDIO_OK;
}
void vorbis_clear_input(VorbisDecoder *d, uint64_t offset)
{
    memset(d->window, 0, DECODER_WINDOW_BYTES); memset(d->pcm, 0, (DECODER_FRAME_MAX + 1) * 2 * sizeof(float));
    d->window_offset = d->validated_offset = offset; d->used = d->pcm_frames = 0;
    d->eof = d->stream_started = d->continued = d->final_seen = false;
    d->packet_bytes = 0; d->previous_granule = 0; d->native_next = 0;
    d->header_packets = 0; d->packet_unknown = false;
}
AudioResult vorbis_restart(VorbisDecoder *d, uint64_t offset, bool resync)
{
    /* A newer seek may supersede a restart before headers reopen. */
    if (resync && d->handle == NULL) { offset = 0; resync = false; }
    vorbis_clear_input(d, offset); d->resync = resync; d->native_known = !resync;
    d->seek_state = SEEK_DECODE;
    (void)audio_convert_init(&d->convert, d->native, d->config.output, d->target);
    if (resync) { stb_vorbis_flush_pushdata(d->handle); }
    else {
        if (d->handle != NULL) { stb_vorbis_close(d->handle); d->handle = NULL; }
        memset(d->allocation.alloc_buffer, 0, (size_t)d->allocation.alloc_buffer_length_in_bytes);
    }
    return AUDIO_OK;
}
