#include "vorbis_test.h"

void vt_conversion_tests(void)
{
    const uint32_t rates[] = {48000, 32000}, channels[] = {2, 1};
    const char *names[] = {"mono22-to-48000-2", "stereo44-to-32000-1"};
    for (size_t i = 0; i < 2; ++i) {
        vt_load(i); size_t samples = vt_load_converted(names[i]);
        size_t expected = samples / channels[i];
        vt_heap_start(); vt_open(rates[i], channels[i], true, DECODER_CODEC_BYTES);
        vt.max_chunk = 37; vt.pending = 2;
        size_t total = vt_decode(vt.actual, VT_PCM_MAX / channels[i]);
        CHECK(total == expected); vt_compare(vt.actual, vt.golden, samples);
        CHECK(decoder_info(vt.decoder).position == expected);
        float output[1024];
        uint64_t targets[] = {0,1,expected / 2,expected - 1,expected};
        for (size_t j = 0; j < 5; ++j) {
            ++vt.expected_generation;
            CHECK(decoder_seek(vt.decoder, vt.expected_generation, targets[j]) == AUDIO_OK);
            size_t got; AudioResult r;
            do { r = decoder_read_frames(vt.decoder, vt.expected_generation, output, 511, &got); } while (r == AUDIO_NEED_MORE);
            size_t want = expected - (size_t)targets[j]; if (want > 511) { want = 511; }
            CHECK(got == want && r == (want == 0 ? AUDIO_EOF : AUDIO_OK));
            vt_compare(output, vt.golden + (size_t)targets[j] * channels[i], want * channels[i]);
        }
        vt_close(); vt_heap_stop();
    }
    uint64_t length = 99;
    CHECK(audio_convert_length(UINT64_MAX, 48000, 44100, &length) == AUDIO_LIMIT && length == 99);
    CHECK(audio_convert_length(12345, 0, 44100, &length) == AUDIO_INVALID);
    AudioConvert c; AudioFormat f = {44100,2,AUDIO_FLOAT32};
    CHECK(audio_convert_init(&c, f, (AudioFormat){44100,3,AUDIO_FLOAT32}, 0) == AUDIO_UNSUPPORTED);
    CHECK(audio_convert_init(&c, f, f, UINT64_MAX) == AUDIO_LIMIT);
}
