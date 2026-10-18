#ifndef SPOTIC_OGG_INDEX_H
#define SPOTIC_OGG_INDEX_H
#include "audio_format.h"
enum { OGG_INDEX_ENTRIES = 64, OGG_HEADER_BYTES = 27, OGG_MAX_PAGE_BYTES = 65307 };
typedef struct OggPage {
    uint64_t offset, granule;
    uint32_t bytes, serial, sequence;
    uint8_t flags, segments;
    bool continued_end;
} OggPage;
typedef struct OggIndex {
    OggPage pages[OGG_INDEX_ENTRIES]; size_t count;
    uint64_t total_frames, file_bytes;
    uint32_t serial, compactions;
    bool serial_known, final_known;
} OggIndex;
/* No table initialization/global state; CRC field [22,26) treated as zero. */
uint32_t ogg_crc(const uint8_t *, size_t);
AudioResult ogg_page_parse(const void *, size_t bytes, uint64_t offset, size_t cap, OggPage *);
/* Probe scans are bounded to supplied bytes, skip false captures/bad CRCs,
 * and return only a complete validated page. Ordinary streaming uses parse. */
AudioResult ogg_page_find(const void *, size_t bytes, uint64_t offset, size_t cap, OggPage *);
AudioResult ogg_index_add(OggIndex *, const OggPage *);
/* Predecessor <= granule, successor > granule. Copies, never borrowed entries. */
void ogg_index_bounds(const OggIndex *, uint64_t granule, OggPage *low, OggPage *high);
#endif
