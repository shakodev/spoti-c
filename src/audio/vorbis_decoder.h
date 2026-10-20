#ifndef SPOTIC_VORBIS_DECODER_H
#define SPOTIC_VORBIS_DECODER_H
#include "audio_format.h"
#include "core/arena.h"

enum { DECODER_CODEC_BYTES = 262144, DECODER_WORK_BYTES = 65536,
    DECODER_WINDOW_BYTES = 28672, DECODER_FRAME_MAX = 4096 };
typedef struct VorbisDecoder VorbisDecoder;
/* Decoder-thread seam into a logical Vorbis source, e.g. #15's decryption
 * handoff. Copies <= capacity bytes into caller memory, no borrowed pointer.
 * OK must make progress; EOF can supply the last bytes. NEED_MORE copies none.
 * Owner arranges range requests and wakes the decoder outside this callback.
 * No UI/WASAPI callback use. The generation identifies every pending request. */
typedef AudioResult (*DecoderReadAt)(void *, uint64_t generation, uint64_t offset,
    void *output, size_t capacity, size_t *read);
typedef struct DecoderConfig {
    AudioFormat output;
    uint64_t generation, source_bytes; /* 1..UINT32_MAX logical bytes, excluding prefix */
    DecoderReadAt read_at; /* Optional for caller-push streaming. Required for seek. */
    void *context;
    uint8_t trailing_zero_limit; /* 0: strict; <=15 checked bytes after a valid final page. */
} DecoderConfig;
typedef struct DecoderInfo {
    AudioFormat native, output;
    uint64_t generation, position, total_frames;
    size_t codec_bytes, codec_peak, work_bytes, buffered_bytes, index_entries;
    uint32_t index_compactions, seek_probes;
    bool ready, length_known, seeking;
} DecoderInfo;
/* One decoder-thread owner for every API; current/next also open serially on
 * that thread (stb's CRC table initialization is not concurrent). Both arenas
 * must be empty, distinct, fixed buffers of <= the named caps. No heap fallback.
 * Close restores both arena marks and wipes their allocated decoder storage.
 * Never reset/rewind them or copy a live decoder. Output format is epoch-fixed.
 * Mutations reject wrong-thread callers and source-callback reentry with
 * BAD_STATE; decoder_info returns an empty snapshot from another thread. */
AudioResult decoder_open(Arena *codec, Arena *work, const DecoderConfig *, VorbisDecoder **);
/* Append at the exact next requested logical byte offset. May accept a prefix
 * and return FULL; retain the remainder and retry after read_frames. EOF applies
 * only if all supplied bytes were accepted, at the exact declared file size.
 * Old generations return STALE without accepting bytes or changing state. */
AudioResult decoder_push(VorbisDecoder *, uint64_t generation, uint64_t offset,
    const void *, size_t bytes, bool eof, size_t *accepted);
AudioResult decoder_input(VorbisDecoder *, uint64_t generation, uint64_t *offset, size_t *capacity);
/* Caller output has capacity * output.channels floats, disjoint from decoder
 * storage. Returns OK for any produced frames; EOF/NEED_MORE only with zero.
 * Check the result on every call; malformed input latches an explicit error. */
AudioResult decoder_read_frames(VorbisDecoder *, uint64_t generation, float *, size_t capacity, size_t *frames);
/* A command/epoch acknowledgement on the decoder thread: clears all old PCM
 * and input immediately. Targets use output-rate frames. Range refinement and
 * exact pre-roll/discard proceed during read_frames and can return NEED_MORE.
 * Strictly newer generation, ready headers and read_at required. The sink must
 * drop its already-enqueued old-epoch PCM separately before acknowledging seek. */
AudioResult decoder_seek(VorbisDecoder *, uint64_t new_generation, uint64_t target_frame);
DecoderInfo decoder_info(const VorbisDecoder *);
AudioResult decoder_close(VorbisDecoder *);
#endif
