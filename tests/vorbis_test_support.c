#include "vorbis_test.h"
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static size_t heap_calls;
static int hook(int kind, void *data, size_t bytes, int use, long request, const unsigned char *file, int line)
{
    (void)data; (void)bytes; (void)use; (void)request; (void)file; (void)line;
    if (kind == _HOOK_ALLOC || kind == _HOOK_REALLOC) { ++heap_calls; } return 1;
}
#endif
VorbisTest vt;
const VorbisFixture vt_corpus[3] = {{"mono22",22050,1,27317}, {"stereo44",44100,2,141127}, {"stereo48",48000,2,432137}};
static _Alignas(16) uint8_t codec_memory[DECODER_CODEC_BYTES], work_memory[DECODER_WORK_BYTES];

void vt_heap_start(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    heap_calls = 0; _CrtSetAllocHook(hook);
#endif
}
void vt_heap_stop(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(NULL); CHECK(heap_calls == 0);
#endif
}
static size_t load(const char *name, const char *suffix, void *data, size_t capacity)
{
    char path[1024]; CHECK(sprintf_s(path, sizeof(path), "%s/%s.%s", VORBIS_FIXTURE_DIR, name, suffix) > 0);
    FILE *file = NULL; CHECK(fopen_s(&file, path, "rb") == 0 && file != NULL);
    size_t bytes = fread(data, 1, capacity, file); CHECK(fgetc(file) == EOF && fclose(file) == 0); return bytes;
}
void vt_load(size_t index)
{
    CHECK(index < 3 && vt.decoder == NULL); memset(&vt, 0, sizeof(vt));
    const VorbisFixture *f = &vt_corpus[index]; vt.rate = f->rate; vt.channels = f->channels; vt.frames = f->frames;
    vt.file_bytes = load(f->name, "ogg", vt.ogg, sizeof(vt.ogg));
    CHECK(load(f->name, "f32", vt.golden, sizeof(vt.golden)) == vt.frames * vt.channels * sizeof(float));
}
size_t vt_load_converted(const char *name)
{
    size_t bytes = load(name, "f32", vt.golden, sizeof(vt.golden));
    CHECK(bytes % sizeof(float) == 0); return bytes / sizeof(float);
}
AudioResult vt_read_at(void *context, uint64_t generation, uint64_t offset, void *output, size_t capacity, size_t *got)
{
    (void)context; CHECK(vt.source_enabled && generation == vt.expected_generation && offset <= vt.file_bytes);
    ++vt.calls; *got = 0;
    if (vt.pending != 0) { --vt.pending; return AUDIO_NEED_MORE; }
    if (vt.read_count < 128) { vt.reads[vt.read_count++] = offset; }
    size_t take = vt.file_bytes - (size_t)offset;
    if (take > capacity) { take = capacity; }
    if (vt.max_chunk != 0 && take > vt.max_chunk) { take = vt.max_chunk; }
    memcpy(output, vt.ogg + (size_t)offset, take); *got = take;
    return offset + take == vt.file_bytes ? AUDIO_EOF : AUDIO_OK;
}
void vt_open(uint32_t rate, uint32_t channels, bool source, size_t codec_bytes)
{
    CHECK(arena_init(&vt.codec, codec_memory, codec_bytes)); CHECK(arena_init(&vt.work, work_memory, sizeof(work_memory)));
    vt.expected_generation = 1; vt.source_enabled = source;
    DecoderConfig config = {.output = {rate, channels, AUDIO_FLOAT32}, .generation = 1,
        .source_bytes = vt.file_bytes, .read_at = source ? vt_read_at : NULL};
    CHECK(decoder_open(&vt.codec, &vt.work, &config, &vt.decoder) == AUDIO_OK);
}
void vt_close(void)
{
    CHECK(decoder_close(vt.decoder) == AUDIO_OK); vt.decoder = NULL;
    CHECK(vt.codec.used == 0 && vt.work.used == 0 && vt.codec.high_water <= DECODER_CODEC_BYTES && vt.work.high_water <= DECODER_WORK_BYTES);
    arena_dispose(&vt.codec); arena_dispose(&vt.work);
    for (size_t i = 0; i < sizeof(codec_memory); ++i) { CHECK(codec_memory[i] == 0); }
    for (size_t i = 0; i < sizeof(work_memory); ++i) { CHECK(work_memory[i] == 0); }
}
void vt_compare(const float *a, const float *b, size_t samples)
{
    for (size_t i = 0; i < samples; ++i) {
        if (!isfinite(a[i]) || fabsf(a[i] - b[i]) > 0.00003f) {
            fprintf(stderr, "PCM mismatch at %zu: %.9g vs %.9g\n", i, (double)a[i], (double)b[i]); CHECK(false);
        }
    }
}
size_t vt_decode(float *output, size_t capacity)
{
    size_t total = 0;
    for (size_t step = 0; step < 1000000; ++step) {
        size_t got = 0;
        AudioResult r = decoder_read_frames(vt.decoder, vt.expected_generation, output + total * decoder_info(vt.decoder).output.channels, capacity - total, &got);
        total += got; CHECK(total <= capacity);
        if (r == AUDIO_EOF) { return total; }
        if (r != AUDIO_OK && r != AUDIO_NEED_MORE) { fprintf(stderr, "decode result %d at frame %zu\n", (int)r, total); CHECK(false); }
        CHECK(total < capacity);
    }
    CHECK(false); return 0;
}
