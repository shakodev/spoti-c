#ifndef SPOTIC_IMAGE_DECODE_H
#define SPOTIC_IMAGE_DECODE_H
#include "core/worker.h"
#define ART_DECODE_BYTES (512u * 1024u)
#define ART_COMPRESSED_BYTES (64u * 1024u)
#define ART_UPLOAD_BYTES (256u * 1024u)
#define ART_MAX_EDGE 256u
#define ART_ORIGINAL_EDGE 512u
typedef enum ArtResult {
    ART_OK, ART_PENDING, ART_FULL, ART_INVALID, ART_STALE, ART_CANCELLED,
    ART_TOO_LARGE, ART_BUDGET, ART_UNSUPPORTED, ART_DECODE_ERROR, ART_IO_ERROR
} ArtResult;
typedef struct ImageStats {
    size_t live_bytes, peak_bytes;
    uint32_t allocations, frees, live_objects;
    uint64_t wic_private_before, wic_private_after, wic_peak_private;
    bool budget_failed, wic_available;
} ImageStats;
/* Worker only. First 64 KiB of workspace may hold compressed input. Remaining
 * workspace is the exclusive stb pool; output is separate <=256 KiB storage.
 * Stats are reset per call. No output is valid on failure. */
ArtResult image_decode(void *workspace, const void *input, size_t bytes,
    unsigned char *rgba, uint32_t edge, const CancelToken *cancel,
    uint32_t *width, uint32_t *height, ImageStats *stats);
/* C COM backend, worker only. allow_codec=false is a deterministic unavailable
 * capability path. WIC internal memory is measured, not arena-controlled. */
ArtResult image_decode_webp(const void *input, size_t bytes, unsigned char *rgba,
    uint32_t edge, const CancelToken *cancel, uint32_t *width, uint32_t *height,
    ImageStats *stats, bool allow_codec);
#endif
