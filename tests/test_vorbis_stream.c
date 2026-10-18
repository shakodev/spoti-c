#include "vorbis_test.h"

static void fragments(size_t corpus, bool single_byte)
{
    vt_load(corpus); vt_heap_start(); vt_open(vt.rate, vt.channels, false, DECODER_CODEC_BYTES);
    size_t offset = 0, total = 0; uint32_t random = 16;
    for (size_t step = 0; step < 2000000; ++step) {
        size_t frames = 0;
        AudioResult r = decoder_read_frames(vt.decoder, 1, vt.actual + total * vt.channels, 511, &frames);
        total += frames; CHECK(total <= vt.frames);
        if (r == AUDIO_EOF) { break; }
        if (r != AUDIO_OK && r != AUDIO_NEED_MORE) { fprintf(stderr, "fragment result %d offset %zu total %zu\n", (int)r, offset, total); CHECK(false); }
        if (r == AUDIO_NEED_MORE) {
            CHECK(offset < vt.file_bytes);
            random = random * 1664525u + 1013904223u;
            size_t bytes = single_byte ? 1 : 1 + random % 7001;
            if (bytes > vt.file_bytes - offset) { bytes = vt.file_bytes - offset; }
            size_t accepted;
            r = decoder_push(vt.decoder, 1, offset, vt.ogg + offset, bytes, offset + bytes == vt.file_bytes, &accepted);
            CHECK(r == AUDIO_OK || r == AUDIO_FULL); offset += accepted;
        }
    }
    CHECK(total == vt.frames && offset == vt.file_bytes);
    vt_compare(vt.actual, vt.golden, total * vt.channels);
    DecoderInfo info = decoder_info(vt.decoder);
    CHECK(info.ready && info.position == vt.frames && info.total_frames == vt.frames);
    CHECK(info.codec_peak <= DECODER_CODEC_BYTES && info.work_bytes <= DECODER_WORK_BYTES);
    printf("%s: codec peak %zu, work %zu\n", vt_corpus[corpus].name, info.codec_peak, info.work_bytes);
    vt_close(); vt_heap_stop();
}
int main(void)
{
    for (size_t i = 0; i < 3; ++i) { fragments(i, true); fragments(i, false); }
    vt_conversion_tests(); vt_error_tests(); vt_limit_tests();
    puts("audio_vorbis_stream passed"); return 0;
}
