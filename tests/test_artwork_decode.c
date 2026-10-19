#include "ui/image_decode.h"
#include "support/test_check.h"
#include "artwork_fixtures.h"
#include <string.h>

static _Alignas(16) unsigned char workspace[ART_DECODE_BYTES];
static unsigned char output[ART_UPLOAD_BYTES + 32];

static CancelToken token(void)
{
    return (CancelToken){.deadline = CORE_CLOCK_NEVER};
}

static void balanced(ImageStats stats)
{
    CHECK(stats.live_bytes == 0 && stats.live_objects == 0);
    CHECK(stats.allocations == stats.frees);
    CHECK(stats.peak_bytes <= ART_DECODE_BYTES - ART_COMPRESSED_BYTES);
    for (size_t i = ART_UPLOAD_BYTES; i < sizeof(output); ++i) {
        CHECK(output[i] == 0xa5);
    }
}

static void pixels(uint32_t width, uint32_t height, unsigned tolerance)
{
    const unsigned expected[] = {32, 96, 160, 255};
    for (size_t i = 0; i < (size_t)width * height * 4; ++i) {
        unsigned actual = output[i], value = expected[i % 4];
        CHECK(actual + tolerance >= value && actual <= value + tolerance);
    }
}

static void valid_images(void)
{
    CancelToken cancel = token();
    uint32_t width, height;
    ImageStats stats;
    memset(output, 0xa5, sizeof(output));
    CHECK(image_decode(workspace, art_fixture_png, sizeof(art_fixture_png), output,
        256, &cancel, &width, &height, &stats) == ART_OK);
    CHECK(width == 4 && height == 2 && stats.allocations > 0);
    pixels(width, height, 0); balanced(stats);
    CHECK(image_decode(workspace, art_fixture_png, sizeof(art_fixture_png), output,
        2, &cancel, &width, &height, &stats) == ART_OK);
    CHECK(width == 2 && height == 1);
    pixels(width, height, 0); balanced(stats);
    CHECK(image_decode(workspace, art_fixture_jpeg, sizeof(art_fixture_jpeg), output,
        4, &cancel, &width, &height, &stats) == ART_OK);
    CHECK(width == 4 && height == 2);
    pixels(width, height, 4); balanced(stats);
    CHECK(image_decode(workspace, art_fixture_jpeg_max, sizeof(art_fixture_jpeg_max), output,
        256, &cancel, &width, &height, &stats) == ART_OK);
    CHECK(width == 256 && height == 256);
    pixels(width, height, 4); balanced(stats);
    CHECK(image_decode(workspace, art_fixture_png_medium, sizeof(art_fixture_png_medium), output,
        256, &cancel, &width, &height, &stats) == ART_OK);
    CHECK(width == 128 && height == 128);
    pixels(width, height, 0); balanced(stats);
    /* Production fetch writes the first 64 KiB of the same workspace. */
    memcpy(workspace, art_fixture_png, sizeof(art_fixture_png));
    CHECK(image_decode(workspace, workspace, sizeof(art_fixture_png), output,
        256, &cancel, &width, &height, &stats) == ART_OK);
    pixels(width, height, 0); balanced(stats);
}

static void malformed_and_budgets(void)
{
    CancelToken cancel = token();
    uint32_t width = 0, height = 0;
    ImageStats stats;
    memset(output, 0xa5, sizeof(output));
    CHECK(image_decode(workspace, art_fixture_png, sizeof(art_fixture_png) / 2,
        output, 256, &cancel, &width, &height, &stats) == ART_DECODE_ERROR);
    balanced(stats);
    CHECK(image_decode(workspace, art_fixture_jpeg, 10, output, 256,
        &cancel, &width, &height, &stats) == ART_DECODE_ERROR);
    balanced(stats);
    unsigned char no_scan[179];
    memcpy(no_scan, art_fixture_jpeg, 177); no_scan[177] = 0xff; no_scan[178] = 0xd9;
    CHECK(image_decode(workspace, no_scan, sizeof(no_scan), output, 256,
        &cancel, &width, &height, &stats) == ART_DECODE_ERROR);
    balanced(stats);
    CHECK(image_decode(workspace, art_fixture_oversize_png, sizeof(art_fixture_oversize_png),
        output, 256, &cancel, &width, &height, &stats) == ART_TOO_LARGE);
    balanced(stats);
    CHECK(image_decode(workspace, art_fixture_bomb_png, sizeof(art_fixture_bomb_png),
        output, 256, &cancel, &width, &height, &stats) == ART_TOO_LARGE);
    balanced(stats);
    CHECK(image_decode(workspace, art_fixture_budget_png, sizeof(art_fixture_budget_png),
        output, 256, &cancel, &width, &height, &stats) == ART_BUDGET);
    CHECK(stats.budget_failed && stats.peak_bytes > 0);
    balanced(stats);
    CHECK(image_decode(workspace, art_fixture_png_max, sizeof(art_fixture_png_max),
        output, 256, &cancel, &width, &height, &stats) == ART_BUDGET);
    CHECK(stats.budget_failed && stats.peak_bytes > 0);
    balanced(stats);
    /* A failed allocation must not poison the next independent decode. */
    CHECK(image_decode(workspace, art_fixture_png, sizeof(art_fixture_png), output,
        256, &cancel, &width, &height, &stats) == ART_OK);
    CHECK(!stats.budget_failed); balanced(stats);
    CHECK(image_decode(workspace, workspace, ART_COMPRESSED_BYTES + 1, output,
        256, &cancel, &width, &height, &stats) == ART_TOO_LARGE);
    balanced(stats);
    atomic_store(&cancel.requested, true);
    CHECK(image_decode(workspace, art_fixture_png, sizeof(art_fixture_png), output,
        256, &cancel, &width, &height, &stats) == ART_CANCELLED);
    balanced(stats);
    atomic_store(&cancel.requested, false);
    CHECK(image_decode(workspace, art_fixture_png, sizeof(art_fixture_png), output,
        ART_MAX_EDGE + 1, &cancel, &width, &height, &stats) == ART_INVALID);
    CHECK(image_decode(workspace, art_fixture_png, sizeof(art_fixture_png), output,
        0, &cancel, &width, &height, &stats) == ART_INVALID);
    static const unsigned char gif[] = "GIF89a\1\0\1\0";
    CHECK(image_decode(workspace, gif, sizeof(gif), output, 256,
        &cancel, &width, &height, &stats) == ART_UNSUPPORTED);
    balanced(stats);
}

static void webp_capability(void)
{
    CancelToken cancel = token();
    uint32_t width = 0, height = 0;
    ImageStats stats;
    memset(output, 0xa5, sizeof(output));
    CHECK(image_decode_webp(art_fixture_webp, sizeof(art_fixture_webp), output,
        256, &cancel, &width, &height, &stats, false) == ART_UNSUPPORTED);
    CHECK(!stats.wic_available); balanced(stats);
    ArtResult result = image_decode_webp(art_fixture_webp, sizeof(art_fixture_webp),
        output, 256, &cancel, &width, &height, &stats, true);
    CHECK(result == ART_OK || result == ART_UNSUPPORTED);
    if (result == ART_OK) {
        CHECK(stats.wic_available && width == 8 && height == 4);
        pixels(width, height, 6);
        CHECK(stats.wic_private_before > 0 && stats.wic_private_after > 0);
        CHECK(stats.wic_peak_private >= stats.wic_private_after);
    }
    balanced(stats);
    result = image_decode_webp(art_fixture_webp_max, sizeof(art_fixture_webp_max),
        output, 256, &cancel, &width, &height, &stats, true);
    CHECK(result == ART_OK || result == ART_UNSUPPORTED);
    if (result == ART_OK) {
        CHECK(stats.wic_available && width == 256 && height == 256);
        pixels(width, height, 6);
        CHECK(stats.wic_peak_private >= stats.wic_private_after);
    }
    balanced(stats);
    atomic_store(&cancel.requested, true);
    CHECK(image_decode_webp(art_fixture_webp, sizeof(art_fixture_webp), output,
        256, &cancel, &width, &height, &stats, true) == ART_CANCELLED);
    balanced(stats);
}

static void webp_container_limits(void)
{
    CancelToken cancel = token();
    uint32_t width, height; ImageStats stats;
    unsigned char changed[sizeof(art_fixture_webp) + 18];
    memcpy(changed, art_fixture_webp, sizeof(art_fixture_webp));
    changed[4] ^= 1; /* Declared RIFF length differs from the supplied body. */
    CHECK(image_decode_webp(changed, sizeof(art_fixture_webp), output, 256,
        &cancel, &width, &height, &stats, false) == ART_DECODE_ERROR);
    balanced(stats);
    memcpy(changed, art_fixture_webp, sizeof(art_fixture_webp));
    changed[16] = 0xff; /* Chunk extends beyond the bounded container. */
    CHECK(image_decode_webp(changed, sizeof(art_fixture_webp), output, 256,
        &cancel, &width, &height, &stats, false) == ART_DECODE_ERROR);
    balanced(stats);
    memcpy(changed, art_fixture_webp, sizeof(art_fixture_webp));
    changed[26] = 1; changed[27] = 2; /* VP8 width = 513. */
    CHECK(image_decode_webp(changed, sizeof(art_fixture_webp), output, 256,
        &cancel, &width, &height, &stats, false) == ART_TOO_LARGE);
    balanced(stats);
    static const unsigned char oversized_lossless[] = {
        'R','I','F','F',18,0,0,0,'W','E','B','P',
        'V','P','8','L',5,0,0,0,0x2f,0,2,0,0,0
    }; /* VP8L width = 513, height = 1; no codec should be invoked. */
    CHECK(image_decode_webp(oversized_lossless, sizeof(oversized_lossless), output,
        256, &cancel, &width, &height, &stats, false) == ART_TOO_LARGE);
    balanced(stats);
    memcpy(changed, art_fixture_webp, 12);
    changed[4] = (unsigned char)(sizeof(changed) - 8);
    memcpy(changed + 12, "VP8X\x0a\0\0\0", 8);
    memset(changed + 20, 0, 10);
    changed[24] = 7; changed[27] = 3; /* Valid 8x4 canvas around the frame. */
    memcpy(changed + 30, art_fixture_webp + 12, sizeof(art_fixture_webp) - 12);
    changed[20] = 2; /* Animation requires a different bounded lifecycle. */
    CHECK(image_decode_webp(changed, sizeof(changed), output, 256,
        &cancel, &width, &height, &stats, false) == ART_UNSUPPORTED);
    balanced(stats);
    changed[20] = 0; changed[24] = 0; changed[25] = 2; /* 513-wide VP8X canvas. */
    CHECK(image_decode_webp(changed, sizeof(changed), output, 256,
        &cancel, &width, &height, &stats, false) == ART_TOO_LARGE);
    balanced(stats);
}

void test_artwork_decode(void)
{
    valid_images();
    malformed_and_budgets();
    webp_capability();
    webp_container_limits();
}
