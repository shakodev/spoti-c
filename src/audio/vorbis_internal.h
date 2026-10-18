#ifndef SPOTIC_VORBIS_INTERNAL_H
#define SPOTIC_VORBIS_INTERNAL_H
#include "vorbis_decoder.h"
#include "ogg_index.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PULLDATA_API
#include "stb_vorbis.c"
#include <string.h>

enum { SEEK_NONE, SEEK_TAIL, SEEK_REFINE, SEEK_DECODE };
struct VorbisDecoder {
    Arena *codec_arena, *work_arena;
    ArenaMark codec_mark, work_mark;
    DecoderConfig config;
    stb_vorbis *handle;
    stb_vorbis_alloc allocation;
    AudioFormat native;
    AudioConvert convert;
    OggIndex index;
    uint8_t *window;
    float *pcm;
    uint64_t window_offset, validated_offset, native_next, pcm_start;
    uint64_t previous_granule, target, probe_offset;
    size_t used, pcm_frames, probe_used, probe_wanted;
    uint32_t sequence, probes, header_packets, owner_thread;
    size_t packet_bytes, setup_peak;
    AudioResult failure;
    unsigned seek_state;
    bool ready, eof, stream_started, continued, final_seen, resync, native_known, packet_unknown, busy;
};
_Static_assert(sizeof(VorbisDecoder) + DECODER_WINDOW_BYTES + (DECODER_FRAME_MAX + 1) * 2 * sizeof(float) < DECODER_WORK_BYTES,
    "two 64 KiB workspaces include windows, indexes, PCM and conversion metadata");
AudioResult vorbis_validate(VorbisDecoder *);
AudioResult vorbis_pump(VorbisDecoder *);
AudioResult vorbis_fill(VorbisDecoder *);
AudioResult vorbis_seek_drive(VorbisDecoder *);
void vorbis_clear_input(VorbisDecoder *, uint64_t offset);
AudioResult vorbis_restart(VorbisDecoder *, uint64_t offset, bool resync);
#endif
