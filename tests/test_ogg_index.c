#include "vorbis_test.h"
void vt_ogg_tests(void)
{
    OggIndex index = {0};
    for (uint32_t i = 0; i < 500; ++i) {
        OggPage page = {.offset = (uint64_t)i * 100, .granule = (uint64_t)i * 1000, .bytes = 100, .serial = 7, .sequence = i};
        CHECK(ogg_index_add(&index, &page) == AUDIO_OK);
    }
    CHECK(index.count <= OGG_INDEX_ENTRIES && index.compactions != 0);
    OggPage low, high; ogg_index_bounds(&index, 250123, &low, &high);
    CHECK(low.granule <= 250123 && high.granule > 250123);
    OggPage changed = index.pages[0]; changed.granule = 1;
    CHECK(ogg_index_add(&index, &changed) == AUDIO_CORRUPT);
    changed = index.pages[0]; changed.serial = 9;
    CHECK(ogg_index_add(&index, &changed) == AUDIO_UNSUPPORTED);
    vt_load(0);
    OggPage page;
    CHECK(ogg_page_parse(vt.ogg, vt.file_bytes, 0, DECODER_WINDOW_BYTES, &page) == AUDIO_OK && page.bytes == 58 && page.flags == 2);
    for (size_t bytes = 0; bytes < 58; ++bytes) { CHECK(ogg_page_parse(vt.ogg, bytes, 0, DECODER_WINDOW_BYTES, &page) == AUDIO_NEED_MORE); }
    uint32_t expected_crc = vt.ogg[22] | ((uint32_t)vt.ogg[23] << 8) | ((uint32_t)vt.ogg[24] << 16) | ((uint32_t)vt.ogg[25] << 24);
    CHECK(ogg_crc(vt.ogg, 58) == expected_crc);
    vt.ogg[32] ^= 1;
    CHECK(ogg_page_parse(vt.ogg, 58, 0, DECODER_WINDOW_BYTES, &page) == AUDIO_CORRUPT);
    CHECK(ogg_page_find(vt.ogg, vt.file_bytes, 0, DECODER_WINDOW_BYTES, &page) == AUDIO_OK && page.offset == 58);
    size_t bytes = page.bytes;
    CHECK(ogg_page_parse(vt.ogg + 58, vt.file_bytes - 58, UINT64_MAX - 20, DECODER_WINDOW_BYTES, &page) == AUDIO_LIMIT);
    vt.ogg[58 + 10] = 1;
    CHECK(ogg_page_parse(vt.ogg + 58, vt.file_bytes - 58, 58, DECODER_WINDOW_BYTES, &page) == AUDIO_CORRUPT);
    uint32_t crc = ogg_crc(vt.ogg + 58, bytes);
    for (unsigned i = 0; i < 4; ++i) { vt.ogg[58 + 22 + i] = (uint8_t)(crc >> (8 * i)); }
    CHECK(ogg_page_parse(vt.ogg + 58, vt.file_bytes - 58, 58, DECODER_WINDOW_BYTES, &page) == AUDIO_LIMIT);
}
