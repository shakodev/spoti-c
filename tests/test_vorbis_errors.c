#include "vorbis_test.h"

static void put32(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) { p[i] = (uint8_t)(n >> (8 * i)); }
}
static void repair(size_t offset)
{
    uint8_t *p = vt.ogg + offset; size_t bytes = 27 + p[26];
    for (size_t i = 0; i < p[26]; ++i) { bytes += p[27 + i]; }
    put32(p + 22, ogg_crc(p, bytes));
}
static AudioResult drain_error(void)
{
    float output[1024];
    for (size_t i = 0; i < 20000; ++i) {
        size_t got; AudioResult r = decoder_read_frames(vt.decoder, 1, output, 512, &got);
        if (r != AUDIO_OK && r != AUDIO_NEED_MORE) { return r; }
    }
    CHECK(false); return AUDIO_BAD_STATE;
}
static void malformed(unsigned kind, AudioResult expected)
{
    vt_load(1);
    if (kind == 0) { vt.ogg[0] = 'X'; }
    if (kind == 1) { vt.ogg[80] ^= 1; }
    if (kind == 2) { put32(vt.ogg + 58 + 18, 2); repair(58); }
    if (kind == 3) { vt.ogg[58 + 14] ^= 1; repair(58); }
    if (kind == 4) { --vt.file_bytes; }
    if (kind == 5) { vt.file_bytes = 57; }
    if (kind == 6) { vt.ogg[39] = 6; repair(0); }
    if (kind == 7) { vt.ogg[4] = 1; repair(0); }
    if (kind == 8) {
        OggPage first, setup;
        CHECK(ogg_page_parse(vt.ogg, vt.file_bytes, 0, DECODER_WINDOW_BYTES, &first) == AUDIO_OK);
        CHECK(ogg_page_parse(vt.ogg + first.bytes, vt.file_bytes - first.bytes, first.bytes, DECODER_WINDOW_BYTES, &setup) == AUDIO_OK);
        size_t at = first.bytes + setup.bytes;
        vt.ogg[at + 27 + vt.ogg[at + 26]] |= 1; repair(at); /* CRC-valid invalid audio packet type */
    }
    vt_heap_start(); vt_open(vt.rate, vt.channels, true, kind == 9 ? 65536 : DECODER_CODEC_BYTES);
    CHECK(drain_error() == expected); CHECK(drain_error() == expected); vt_close(); vt_heap_stop();
}
void vt_error_tests(void)
{
    malformed(0,AUDIO_CORRUPT); malformed(1,AUDIO_CORRUPT); malformed(2,AUDIO_CORRUPT);
    malformed(3,AUDIO_UNSUPPORTED); malformed(4,AUDIO_TRUNCATED); malformed(5,AUDIO_TRUNCATED);
    malformed(6,AUDIO_UNSUPPORTED); malformed(7,AUDIO_CORRUPT); malformed(8,AUDIO_CORRUPT);
    malformed(9,AUDIO_NO_MEMORY);
    vt_load(0); vt_heap_start(); vt_open(vt.rate, vt.channels, false, DECODER_CODEC_BYTES);
    size_t accepted = 99, got = 99; float output[8];
    CHECK(decoder_read_frames(vt.decoder, 1, output, 8, &got) == AUDIO_NEED_MORE && got == 0);
    CHECK(decoder_seek(vt.decoder, 2, 0) == AUDIO_BAD_STATE);
    CHECK(decoder_push(vt.decoder, 2, 0, vt.ogg, 58, false, &accepted) == AUDIO_STALE && accepted == 0);
    CHECK(decoder_read_frames(vt.decoder, 2, output, 8, &got) == AUDIO_STALE && got == 0);
    CHECK(decoder_push(vt.decoder, 1, 1, vt.ogg, 58, false, &accepted) == AUDIO_INVALID && accepted == 0);
    CHECK(decoder_push(vt.decoder, 1, 0, vt.ogg, 58, false, &accepted) == AUDIO_OK && accepted == 58);
    CHECK(vt.calls == 0); /* push/metadata never invoke read_at or decode callback work */
    /* Valid capture/lacing declares a page larger than the fixed input cap. */
    uint8_t page[27 + 113] = {0}; memcpy(page, "OggS", 4); page[26] = 113;
    memset(page + 27, 255, 113);
    CHECK(decoder_push(vt.decoder, 1, 58, page, sizeof(page), false, &accepted) == AUDIO_LIMIT);
    vt_close(); vt_heap_stop();
    /* Missing EOS despite complete, CRC-valid pages is a truncated stream. */
    vt_load(0); size_t at = 0;
    while (at < vt.file_bytes) {
        OggPage page_info; CHECK(ogg_page_parse(vt.ogg + at, vt.file_bytes - at, at, DECODER_WINDOW_BYTES, &page_info) == AUDIO_OK);
        if ((page_info.flags & 4) != 0) { vt.ogg[at + 5] &= 0xfb; repair(at); }
        at += page_info.bytes;
    }
    vt_heap_start(); vt_open(vt.rate, vt.channels, true, DECODER_CODEC_BYTES);
    CHECK(drain_error() == AUDIO_TRUNCATED); vt_close(); vt_heap_stop();
}
