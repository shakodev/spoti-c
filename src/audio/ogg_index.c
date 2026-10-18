#include "ogg_index.h"
#include <limits.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint32_t ogg_crc(const uint8_t *p, size_t bytes)
{
    uint32_t crc = 0;
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= (uint32_t)(i >= 22 && i < 26 ? 0 : p[i]) << 24;
        for (unsigned bit = 0; bit < 8; ++bit) { crc = (crc << 1) ^ ((crc >> 31) != 0 ? UINT32_C(0x04c11db7) : 0); }
    }
    return crc;
}
AudioResult ogg_page_parse(const void *data, size_t bytes, uint64_t offset, size_t cap, OggPage *out)
{
    if (out == NULL || (data == NULL && bytes != 0) || cap < OGG_HEADER_BYTES) { return AUDIO_INVALID; }
    if (bytes < OGG_HEADER_BYTES) { return AUDIO_NEED_MORE; }
    const uint8_t *p = data;
    if (memcmp(p, "OggS", 4) != 0 || p[4] != 0 || (p[5] & ~7u) != 0) { return AUDIO_CORRUPT; }
    size_t segments = p[26], length = OGG_HEADER_BYTES + segments;
    if (bytes < length) { return AUDIO_NEED_MORE; }
    for (size_t i = 0; i < segments; ++i) { length += p[27 + i]; }
    if (length > cap || length > UINT64_MAX - offset) { return AUDIO_LIMIT; }
    if (bytes < length) { return AUDIO_NEED_MORE; }
    if (ogg_crc(p, length) != le32(p + 22)) { return AUDIO_CORRUPT; }
    uint64_t granule = le32(p + 6) | ((uint64_t)le32(p + 10) << 32);
    if (granule != UINT64_MAX && granule > INT32_MAX) { return AUDIO_LIMIT; }
    *out = (OggPage){offset, granule, (uint32_t)length, le32(p + 14), le32(p + 18),
        p[5], p[26], segments != 0 && p[26 + segments] == 255};
    return AUDIO_OK;
}
AudioResult ogg_page_find(const void *data, size_t bytes, uint64_t offset, size_t cap, OggPage *out)
{
    if (data == NULL || out == NULL || bytes > UINT64_MAX - offset) { return AUDIO_INVALID; }
    const uint8_t *p = data;
    for (size_t i = 0; i + OGG_HEADER_BYTES <= bytes; ++i) {
        if (memcmp(p + i, "OggS", 4) != 0) { continue; }
        if (ogg_page_parse(p + i, bytes - i, offset + i, cap, out) == AUDIO_OK) { return AUDIO_OK; }
    }
    return AUDIO_NEED_MORE;
}
AudioResult ogg_index_add(OggIndex *index, const OggPage *page)
{
    if (index == NULL || page == NULL || page->bytes == 0 || page->bytes > UINT64_MAX - page->offset) { return AUDIO_INVALID; }
    if (index->serial_known && index->serial != page->serial) { return AUDIO_UNSUPPORTED; }
    index->serial_known = true; index->serial = page->serial;
    if ((page->flags & 4) != 0) {
        if (page->granule == UINT64_MAX || page->continued_end) { return AUDIO_CORRUPT; }
        if (index->final_known && (index->total_frames != page->granule || index->file_bytes != page->offset + page->bytes)) { return AUDIO_CORRUPT; }
        index->total_frames = page->granule; index->file_bytes = page->offset + page->bytes; index->final_known = true;
    }
    if (page->granule == UINT64_MAX || page->continued_end) { return AUDIO_OK; }
    size_t at = 0;
    while (at < index->count && index->pages[at].offset < page->offset) { ++at; }
    if (at < index->count && index->pages[at].offset == page->offset) {
        const OggPage *known = &index->pages[at];
        return known->granule == page->granule && known->bytes == page->bytes && known->serial == page->serial &&
            known->sequence == page->sequence && known->flags == page->flags && known->segments == page->segments &&
            known->continued_end == page->continued_end ? AUDIO_OK : AUDIO_CORRUPT;
    }
    if ((at != 0 && index->pages[at - 1].granule > page->granule) ||
        (at < index->count && index->pages[at].granule < page->granule)) { return AUDIO_CORRUPT; }
    if (index->count == OGG_INDEX_ENTRIES) {
        size_t keep = 1;
        for (size_t i = 2; i < index->count; i += 2) { index->pages[keep++] = index->pages[i]; }
        index->pages[keep++] = index->pages[index->count - 1]; index->count = keep; ++index->compactions;
        at = 0; while (at < index->count && index->pages[at].offset < page->offset) { ++at; }
    }
    memmove(&index->pages[at + 1], &index->pages[at], (index->count - at) * sizeof(*page));
    index->pages[at] = *page; ++index->count; return AUDIO_OK;
}
void ogg_index_bounds(const OggIndex *index, uint64_t granule, OggPage *low, OggPage *high)
{
    *low = (OggPage){0}; *high = (OggPage){0};
    for (size_t i = 0; i < index->count; ++i) {
        if (index->pages[i].granule <= granule) { *low = index->pages[i]; }
        else { *high = index->pages[i]; break; }
    }
}
