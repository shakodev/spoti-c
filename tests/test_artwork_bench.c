#include "ui/image_decode.h"
#include "support/test_check.h"
#include "artwork_fixtures.h"
#include <string.h>

#if defined(SPOTIC_ASAN)
#error Artwork process-memory observations require a Release build without ASan.
#endif

static unsigned char output[ART_UPLOAD_BYTES + 32];

static bool observe(const char *label, const unsigned char *input, size_t bytes,
    uint32_t expected_width, uint32_t expected_height)
{
    CancelToken cancel = {.deadline = CORE_CLOCK_NEVER};
    uint64_t baseline = 0, warm_private = 0, final_private = 0;
    uint64_t maximum_private = 0, process_peak = 0;
    for (unsigned i = 0; i < 100; ++i) {
        uint32_t width = 0, height = 0;
        ImageStats stats;
        memset(output, 0xa5, sizeof(output));
        ArtResult result = image_decode_webp(input, bytes,
            output, 256, &cancel, &width, &height, &stats, true);
        CHECK(stats.live_objects == 0 && stats.live_bytes == 0);
        if (result == ART_UNSUPPORTED) {
            CHECK(!stats.wic_available);
            puts("artwork WIC: installed WebP codec unavailable; placeholder capability path");
            return false;
        }
        CHECK(result == ART_OK && stats.wic_available &&
            width == expected_width && height == expected_height);
        CHECK(stats.wic_private_before > 0 && stats.wic_private_after > 0);
        if (!i) { baseline = stats.wic_private_before; }
        if (i == 9) { warm_private = stats.wic_private_after; }
        final_private = stats.wic_private_after;
        if (stats.wic_private_before > maximum_private) { maximum_private = stats.wic_private_before; }
        if (stats.wic_private_after > maximum_private) { maximum_private = stats.wic_private_after; }
        if (stats.wic_peak_private > process_peak) { process_peak = stats.wic_peak_private; }
        size_t final_pixel = ((size_t)width * height - 1) * 4;
        const unsigned expected[4] = {32,96,160,255};
        for (unsigned channel = 0; channel < 4; ++channel) {
            unsigned actual = output[final_pixel + channel];
            CHECK(actual + 6 >= expected[channel] && actual <= expected[channel] + 6);
        }
        for (size_t at = ART_UPLOAD_BYTES; at < sizeof(output); ++at) { CHECK(output[at] == 0xa5); }
    }
    CHECK(process_peak >= maximum_private);
    printf("artwork WIC %s 100 decodes: baseline_private=%llu warm_private=%llu max_observed_private=%llu final_private=%llu lifetime_peak_private=%llu retained_delta=%lld post_warmup_delta=%lld owned_COM=0\n",
        label, (unsigned long long)baseline, (unsigned long long)warm_private,
        (unsigned long long)maximum_private, (unsigned long long)final_private,
        (unsigned long long)process_peak, (long long)final_private - (long long)baseline,
        (long long)final_private - (long long)warm_private);
    return true;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && !strcmp(argv[1], "--bench"));
    if (!observe("8x4", art_fixture_webp, sizeof(art_fixture_webp), 8, 4)) { return 0; }
    if (!observe("512x512-to-256x256", art_fixture_webp_max,
        sizeof(art_fixture_webp_max), 256, 256)) { return 0; }
    puts("Process observations include COM/codec caches and other native allocations; WIC internals are not bounded by the stb arena.");
    return 0;
}
