#ifndef SPOTIC_VORBIS_TEST_H
#define SPOTIC_VORBIS_TEST_H
#include "audio/vorbis_internal.h"
#include "support/test_check.h"
#include <math.h>
enum { VT_OGG_MAX = 400000, VT_PCM_MAX = 900000 };
typedef struct VorbisFixture {
    const char *name; uint32_t rate, channels; size_t frames;
} VorbisFixture;
typedef struct VorbisTest {
    Arena codec, work;
    VorbisDecoder *decoder;
    size_t file_bytes, frames, calls, pending, max_chunk;
    uint32_t rate, channels;
    uint64_t expected_generation;
    bool source_enabled;
    uint64_t reads[128]; size_t read_count;
    uint8_t ogg[VT_OGG_MAX];
    float golden[VT_PCM_MAX], actual[VT_PCM_MAX];
} VorbisTest;
extern VorbisTest vt;
extern const VorbisFixture vt_corpus[3];
void vt_load(size_t index);
void vt_open(uint32_t rate, uint32_t channels, bool source, size_t codec_bytes);
void vt_close(void);
AudioResult vt_read_at(void *, uint64_t generation, uint64_t offset, void *, size_t, size_t *);
void vt_compare(const float *, const float *, size_t samples);
size_t vt_decode(float *, size_t capacity);
void vt_heap_start(void);
void vt_heap_stop(void);
void vt_ogg_tests(void);
void vt_error_tests(void);
void vt_conversion_tests(void);
void vt_limit_tests(void);
size_t vt_load_converted(const char *name);
#endif
