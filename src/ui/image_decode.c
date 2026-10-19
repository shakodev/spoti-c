#include "image_decode.h"
#include <string.h>
#include <limits.h>
#if defined(SPOTIC_ASAN)
#include <sanitizer/asan_interface.h>
#define POISON(p,n) __asan_poison_memory_region(p,n)
#define UNPOISON(p,n) __asan_unpoison_memory_region(p,n)
#else
#define POISON(p,n) ((void)(p),(void)(n))
#define UNPOISON(p,n) ((void)(p),(void)(n))
#endif

typedef struct ImageBlock { size_t start, span, bytes; } ImageBlock;
typedef struct ImagePool {
    unsigned char *base;
    ImageBlock blocks[96];
    ImageStats *stats;
} ImagePool;
static _Thread_local ImagePool *current_pool;

static void *image_alloc(size_t bytes)
{
    ImagePool *p = current_pool;
    size_t position = ART_COMPRESSED_BYTES, span;
    unsigned slot;
    if (!p || !bytes) return NULL;
    if (bytes > ART_DECODE_BYTES - ART_COMPRESSED_BYTES - 32u) goto fail;
    span = ((bytes + 15u) & ~(size_t)15u) + 16u;
    for (slot = 0; slot < 96 && p->blocks[slot].span; ++slot) {}
    if (slot == 96) goto fail;
    for (;;) {
        size_t next = ART_DECODE_BYTES, advance = position;
        for (unsigned i = 0; i < 96; ++i) {
            ImageBlock b = p->blocks[i];
            if (!b.span) continue;
            if (b.start <= position && b.start + b.span > position) advance = b.start + b.span;
            else if (b.start > position && b.start < next) next = b.start;
        }
        if (advance != position) { position = advance; continue; }
        if (span <= next - position) break;
        if (next == ART_DECODE_BYTES) goto fail;
        position = next;
    }
    p->blocks[slot] = (ImageBlock){position, span, bytes};
    p->stats->live_bytes += bytes;
    if (p->stats->live_bytes > p->stats->peak_bytes) p->stats->peak_bytes = p->stats->live_bytes;
    ++p->stats->allocations;
    UNPOISON(p->base + position, bytes);
    memset(p->base + position, 0, bytes); /* stb tolerates some damaged scans. */
    return p->base + position;
fail:
    p->stats->budget_failed = true;
    return NULL;
}
static void image_free(void *ptr)
{
    ImagePool *p = current_pool;
    if (!ptr) return;
    for (unsigned i = 0; i < 96; ++i) if (p->blocks[i].span && p->base + p->blocks[i].start == ptr) {
        ImageBlock b = p->blocks[i];
        POISON(ptr, b.span);
        p->stats->live_bytes -= b.bytes;
        ++p->stats->frees;
        p->blocks[i] = (ImageBlock){0};
        return;
    }
}
static void *image_realloc(void *ptr, size_t bytes)
{
    size_t previous = 0;
    if (ptr) for (unsigned i = 0; i < 96; ++i)
        if (current_pool->base + current_pool->blocks[i].start == ptr)
            previous = current_pool->blocks[i].bytes;
    void *out = image_alloc(bytes);
    if (out && ptr) { memcpy(out, ptr, previous < bytes ? previous : bytes); image_free(ptr); }
    return out;
}
#define STBI_MALLOC(n) image_alloc(n)
#define STBI_REALLOC(p,n) image_realloc(p,n)
#define STBI_FREE(p) image_free(p)
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS ART_ORIGINAL_EDGE
#define STB_IMAGE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4242 4244 4245 4267 4456 4457 4701)
#endif
#include "stb_image.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

static uint32_t png_u32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
/* stb salvages some malformed JPEGs, including EOI immediately after SOF.
 * Validate the container/scan coverage before allowing any decoded output. */
static ArtResult jpeg_preflight(const unsigned char *p, size_t bytes)
{
    unsigned char ids[4] = {0};
    unsigned components = 0, covered = 0, scans = 0;
    for (size_t at = 2; at < bytes;) {
        if (p[at++] != 0xff) return ART_DECODE_ERROR;
        while (at < bytes && p[at] == 0xff) ++at;
        if (at == bytes) return ART_DECODE_ERROR;
        unsigned marker = p[at++];
        if (marker == 0xd9) return scans && covered == (1u << components) - 1u && at == bytes ? ART_OK : ART_DECODE_ERROR;
        if (bytes - at < 2) return ART_DECODE_ERROR;
        size_t length = (size_t)p[at] * 256 + p[at + 1];
        if (length < 2 || length > bytes - at) return ART_DECODE_ERROR;
        const unsigned char *data = p + at + 2;
        size_t payload = length - 2;
        if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
            if (components || payload < 6 || !data[5] || data[5] > 4 || payload != 6u + 3u * data[5]) return ART_DECODE_ERROR;
            uint32_t w = (uint32_t)data[3] * 256 + data[4], h = (uint32_t)data[1] * 256 + data[2];
            if (!w || !h) return ART_DECODE_ERROR;
            if (w > ART_ORIGINAL_EDGE || h > ART_ORIGINAL_EDGE) return ART_TOO_LARGE;
            components = data[5];
            for (unsigned i = 0; i < components; ++i) {
                ids[i] = data[6 + i * 3];
                for (unsigned j = 0; j < i; ++j) if (ids[i] == ids[j]) return ART_DECODE_ERROR;
            }
        } else if (marker == 0xda) {
            if (!components || payload < 4 || !data[0] || data[0] > components || payload != 4u + 2u * data[0]) return ART_DECODE_ERROR;
            unsigned seen = 0;
            for (unsigned i = 0; i < data[0]; ++i) {
                unsigned j;
                for (j = 0; j < components && ids[j] != data[1 + i * 2]; ++j) {}
                if (j == components || (seen & (1u << j))) return ART_DECODE_ERROR;
                seen |= 1u << j;
            }
            if (!data[1 + data[0] * 2] && !(data[3 + data[0] * 2] >> 4)) covered |= seen;
        } else if (!(marker == 0xc4 || marker == 0xdb || marker == 0xdd || marker == 0xfe || (marker >= 0xe0 && marker <= 0xef))) {
            return ART_DECODE_ERROR;
        }
        at += length;
        if (marker == 0xda) {
            size_t start = at;
            while (at < bytes) {
                if (p[at] != 0xff) { ++at; continue; }
                if (bytes - at < 2) return ART_DECODE_ERROR;
                unsigned next = p[at + 1];
                if (!next || (next >= 0xd0 && next <= 0xd7)) { at += 2; continue; }
                break;
            }
            if (at == start) return ART_DECODE_ERROR;
            ++scans;
        }
    }
    return ART_DECODE_ERROR;
}
static ArtResult png_preflight(const unsigned char *p, size_t bytes)
{
    if (bytes < 33 || png_u32(p + 8) != 13 || memcmp(p + 12, "IHDR", 4)) return ART_DECODE_ERROR;
    uint32_t w = png_u32(p + 16), h = png_u32(p + 20);
    if (!w || !h) return ART_DECODE_ERROR;
    if (w > ART_ORIGINAL_EDGE || h > ART_ORIGINAL_EDGE) return ART_TOO_LARGE;
    bool pixels = false;
    for (size_t at = 8; at < bytes;) {
        if (bytes - at < 12) return ART_DECODE_ERROR;
        size_t length = png_u32(p + at);
        if (length > bytes - at - 12) return ART_DECODE_ERROR;
        const unsigned char *tag = p + at + 4;
        uint32_t crc = UINT32_MAX;
        for (size_t i = 0; i < length + 4; ++i) {
            crc ^= tag[i];
            for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        if ((crc ^ UINT32_MAX) != png_u32(tag + 4 + length)) return ART_DECODE_ERROR;
        if (!memcmp(tag, "IDAT", 4)) pixels = true;
        if (!memcmp(tag, "acTL", 4)) return ART_UNSUPPORTED;
        if (!memcmp(tag, "IEND", 4))
            return pixels && !length && at + 12 == bytes ? ART_OK : ART_DECODE_ERROR;
        at += length + 12;
    }
    return ART_DECODE_ERROR;
}

ArtResult image_decode(void *workspace, const void *input, size_t bytes,
    unsigned char *rgba, uint32_t edge, const CancelToken *cancel,
    uint32_t *width, uint32_t *height, ImageStats *stats)
{
    int w = 0, h = 0, channels = 0;
    unsigned char *original = NULL;
    ArtResult result = ART_DECODE_ERROR;
    ImagePool pool = {.base = workspace, .stats = stats};
    if (width) *width = 0;
    if (height) *height = 0;
    if (stats) *stats = (ImageStats){0};
    if (!workspace || ((uintptr_t)workspace & 15u) || !input || !rgba || !width || !height ||
        !stats || !edge || edge > ART_MAX_EDGE || current_pool) return ART_INVALID;
    if (!bytes || bytes > ART_COMPRESSED_BYTES) return bytes ? ART_TOO_LARGE : ART_DECODE_ERROR;
    if (worker_cancelled(cancel)) return ART_CANCELLED;
    const unsigned char *src = input;
    if (bytes >= 12 && !memcmp(src, "RIFF", 4) && !memcmp(src + 8, "WEBP", 4))
        return image_decode_webp(input, bytes, rgba, edge, cancel, width, height, stats, true);
    if (!(bytes >= 8 && !memcmp(src, "\x89PNG\r\n\x1a\n", 8)) &&
        !(bytes >= 2 && src[0] == 0xff && src[1] == 0xd8)) return ART_UNSUPPORTED;
    if (src[0] == 0x89) {
        ArtResult preflight = png_preflight(src, bytes);
        if (preflight != ART_OK) return preflight;
    } else {
        ArtResult preflight = jpeg_preflight(src, bytes);
        if (preflight != ART_OK) return preflight;
    }
    current_pool = &pool;
    POISON(pool.base + ART_COMPRESSED_BYTES, ART_DECODE_BYTES - ART_COMPRESSED_BYTES);
    if (!stbi_info_from_memory(src, (int)bytes, &w, &h, &channels)) goto done;
    if (w <= 0 || h <= 0 || w > ART_ORIGINAL_EDGE || h > ART_ORIGINAL_EDGE ||
        (uint64_t)(unsigned)w * (unsigned)h > ART_ORIGINAL_EDGE * ART_ORIGINAL_EDGE) {
        result = ART_TOO_LARGE; goto done;
    }
    original = stbi_load_from_memory(src, (int)bytes, &w, &h, &channels, 4);
    if (!original) goto done;
    uint32_t ow = (uint32_t)w, oh = (uint32_t)h;
    if (ow > edge || oh > edge) {
        if (ow >= oh) { oh = (uint32_t)((uint64_t)oh * edge / ow); ow = edge; }
        else { ow = (uint32_t)((uint64_t)ow * edge / oh); oh = edge; }
        if (!ow) ow = 1;
        if (!oh) oh = 1;
    }
    for (uint32_t y = 0; y < oh; ++y) {
        if (worker_cancelled(cancel)) { result = ART_CANCELLED; goto done; }
        for (uint32_t x = 0; x < ow; ++x) {
            size_t from = ((size_t)(y * (uint32_t)h / oh) * (uint32_t)w + x * (uint32_t)w / ow) * 4;
            memcpy(rgba + ((size_t)y * ow + x) * 4, original + from, 4);
        }
    }
    *width = ow; *height = oh; result = ART_OK;
done:
    stbi_image_free(original);
    if (stats->budget_failed && result != ART_OK) result = ART_BUDGET;
    /* All stb failure paths must release their temporary allocations. */
    if (stats->live_bytes) result = ART_DECODE_ERROR;
    if (result != ART_OK) { *width = 0; *height = 0; }
    UNPOISON(pool.base + ART_COMPRESSED_BYTES, ART_DECODE_BYTES - ART_COMPRESSED_BYTES);
    current_pool = NULL;
    return result;
}
