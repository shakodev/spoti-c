#include "vorbis_test.h"
static float output[4096 * 2];

static size_t read_some(size_t capacity)
{
    for (size_t i = 0; i < 100000; ++i) {
        size_t got = 99;
        AudioResult r = decoder_read_frames(vt.decoder, vt.expected_generation, output, capacity, &got);
        if (r == AUDIO_NEED_MORE) { CHECK(got == 0); continue; }
        CHECK(r == AUDIO_OK || r == AUDIO_EOF); return got;
    }
    CHECK(false); return 0;
}
static void check_seek(uint64_t target, bool linear)
{
    uint64_t old = vt.expected_generation; ++vt.expected_generation;
    CHECK(decoder_seek(vt.decoder, vt.expected_generation, target) == AUDIO_OK);
    CHECK(decoder_info(vt.decoder).generation == vt.expected_generation);
    CHECK(decoder_info(vt.decoder).position == target && vt.decoder->pcm_frames == 0 && vt.decoder->used == 0);
    size_t got = 99, accepted = 99;
    CHECK(decoder_read_frames(vt.decoder, old, output, 511, &got) == AUDIO_STALE && got == 0);
    CHECK(decoder_push(vt.decoder, old, 0, vt.ogg, 1, false, &accepted) == AUDIO_STALE && accepted == 0);
    got = read_some(511);
    size_t want = vt.frames - (size_t)target; if (want > 511) { want = 511; }
    CHECK(got == want); vt_compare(output, vt.golden + (size_t)target * vt.channels, want * vt.channels);
    if (linear) { vt_compare(output, vt.actual + (size_t)target * vt.channels, want * vt.channels); }
    CHECK(decoder_info(vt.decoder).position == target + got);
}
static void cold_refinement_and_cancel(void)
{
    vt_load(2); vt_heap_start(); vt_open(vt.rate, vt.channels, true, DECODER_CODEC_BYTES);
    CHECK(read_some(1000) == 1000);
    vt.pending = 1; ++vt.expected_generation;
    CHECK(decoder_seek(vt.decoder, vt.expected_generation, 220000) == AUDIO_OK);
    size_t got = 99;
    CHECK(decoder_read_frames(vt.decoder, vt.expected_generation, output, 511, &got) == AUDIO_NEED_MORE && got == 0);
    CHECK(vt.decoder->seek_state == SEEK_TAIL);
    vt.max_chunk = 137;
    check_seek(320000, false);
    CHECK(decoder_info(vt.decoder).seek_probes >= 2 && vt.decoder->resync);
    CHECK(vt.decoder->window_offset > 0);
    /* Cancel after a restart-to-zero retires the old handle, while replacement
     * headers are pending. A newer nonzero seek must remain safe. */
    vt.pending = 1; ++vt.expected_generation;
    CHECK(decoder_seek(vt.decoder, vt.expected_generation, 0) == AUDIO_OK);
    CHECK(decoder_read_frames(vt.decoder, vt.expected_generation, output, 511, &got) == AUDIO_NEED_MORE);
    CHECK(vt.decoder->handle == NULL);
    check_seek(230000, false);
    for (size_t i = 0; i < 50; ++i) {
        ++vt.expected_generation; CHECK(decoder_seek(vt.decoder, vt.expected_generation, 1000 + i) == AUDIO_OK);
    }
    got = read_some(511); CHECK(got == 511); vt_compare(output, vt.golden + 1049 * 2, 511 * 2);
    vt_close(); vt_heap_stop();
}
static void linear_and_boundaries(size_t corpus)
{
    vt_load(corpus); vt_heap_start(); vt_open(vt.rate, vt.channels, true, DECODER_CODEC_BYTES);
    CHECK(vt_decode(vt.actual, VT_PCM_MAX / vt.channels) == vt.frames);
    vt_compare(vt.actual, vt.golden, vt.frames * vt.channels);
    const uint64_t basic[] = {0,1,15,16,17,vt.frames / 2,vt.frames - 1,vt.frames};
    for (size_t i = 0; i < sizeof(basic)/sizeof(basic[0]); ++i) { check_seek(basic[i], true); }
    size_t offset = 0;
    while (offset < vt.file_bytes) {
        OggPage page;
        CHECK(ogg_page_parse(vt.ogg + offset, vt.file_bytes - offset, offset, DECODER_WINDOW_BYTES, &page) == AUDIO_OK);
        if (page.granule > 0 && page.granule < vt.frames) {
            check_seek(page.granule - 1, true); check_seek(page.granule, true); check_seek(page.granule + 1, true);
        }
        offset += page.bytes;
    }
    uint32_t random = 0x16;
    for (size_t i = 0; i < 24; ++i) {
        random = random * 1664525u + 1013904223u; check_seek(random % vt.frames, true);
    }
    CHECK(decoder_seek(vt.decoder, vt.expected_generation, 0) == AUDIO_STALE);
    CHECK(decoder_seek(vt.decoder, vt.expected_generation + 1, vt.frames + 1) == AUDIO_INVALID);
    CHECK(decoder_info(vt.decoder).generation == vt.expected_generation);
    vt_close(); vt_heap_stop();
}
static void trailing_padding(void)
{
    const size_t sizes[]={1,2,15};
    for (size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
        vt_load(2); size_t end=vt.file_bytes;
        memset(vt.ogg+end,0,sizes[i]); vt.file_bytes+=sizes[i];
        vt_heap_start(); vt_open(vt.rate,vt.channels,true,DECODER_CODEC_BYTES);
        vt.decoder->config.trailing_zero_limit=15;
        CHECK(read_some(1000)==1000 && !decoder_info(vt.decoder).length_known);
        vt.max_chunk=137; check_seek(220000,false);
        CHECK(vt.decoder->config.source_bytes==end);
        vt_close(); vt.max_chunk=137; vt_open(vt.rate,vt.channels,true,DECODER_CODEC_BYTES);
        vt.decoder->config.trailing_zero_limit=15;
        CHECK(vt_decode(vt.actual,VT_PCM_MAX/vt.channels)==vt.frames);
        vt_compare(vt.actual,vt.golden,vt.frames*vt.channels);
        CHECK(vt.decoder->config.source_bytes==end);
        vt_close(); vt_heap_stop();
    }
    for (unsigned fault=0;fault<3;++fault) {
        vt_load(2); size_t padding=fault==1 ? 16 : 2;
        memset(vt.ogg+vt.file_bytes,0,padding);
        if (fault==2) { vt.ogg[vt.file_bytes+1]=1; }
        vt.file_bytes+=padding;
        vt_heap_start(); vt_open(vt.rate,vt.channels,true,DECODER_CODEC_BYTES);
        vt.decoder->config.trailing_zero_limit=fault==0 ? 0 : 15;
        CHECK(read_some(1000)==1000); ++vt.expected_generation;
        CHECK(decoder_seek(vt.decoder,vt.expected_generation,220000)==AUDIO_OK);
        size_t got=99; AudioResult r=decoder_read_frames(vt.decoder,vt.expected_generation,output,511,&got);
        CHECK(r==(fault==2 ? AUDIO_CORRUPT : AUDIO_UNSUPPORTED) && got==0);
        vt_close(); vt_open(vt.rate,vt.channels,true,DECODER_CODEC_BYTES);
        vt.decoder->config.trailing_zero_limit=fault==0 ? 0 : 15;
        do { r=decoder_read_frames(vt.decoder,vt.expected_generation,output,511,&got); }
        while (r==AUDIO_OK || r==AUDIO_NEED_MORE);
        CHECK(r==(fault==2 ? AUDIO_CORRUPT : AUDIO_UNSUPPORTED));
        vt_close(); vt_heap_stop();
    }
}
int main(void)
{
    trailing_padding();
    cold_refinement_and_cancel();
    for (size_t i = 0; i < 3; ++i) { linear_and_boundaries(i); }
    vt_ogg_tests(); puts("audio_seek passed (range refinement, pre-roll, page boundaries, stale epochs)"); return 0;
}
