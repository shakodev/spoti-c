#ifndef SPOTIC_AUDIO_FORMAT_H
#define SPOTIC_AUDIO_FORMAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum AudioSampleFormat { AUDIO_FLOAT32 = 1 } AudioSampleFormat;
typedef struct AudioFormat { uint32_t sample_rate, channels; AudioSampleFormat sample_format; } AudioFormat;
typedef enum AudioResult { AUDIO_OK, AUDIO_NEED_MORE, AUDIO_FULL, AUDIO_EOF,
    AUDIO_INVALID, AUDIO_CORRUPT, AUDIO_TRUNCATED, AUDIO_LIMIT, AUDIO_NO_MEMORY,
    AUDIO_UNSUPPORTED, AUDIO_STALE, AUDIO_BAD_STATE } AudioResult;
typedef struct AudioConvert {
    AudioFormat input, output;
    uint64_t position, tail_position;
    float tail[2];
    bool tail_valid;
} AudioConvert;
bool audio_format_valid(AudioFormat);
AudioResult audio_convert_init(AudioConvert *, AudioFormat input, AudioFormat output, uint64_t position);
AudioResult audio_convert_length(uint64_t native_frames, uint32_t input_rate, uint32_t output_rate, uint64_t *);
/* Fixed linear interpolation on the global output frame grid. Input is copied
 * interleaved float; no retained pointer, allocation, or device work. At a block
 * boundary, retains only the last native frame for the next interpolation.
 * consumed becomes true when the current block can be retired. */
AudioResult audio_convert_block(AudioConvert *, const float *, size_t frames, uint64_t start,
    bool final, float *, size_t capacity, size_t *written, bool *consumed);
#endif
