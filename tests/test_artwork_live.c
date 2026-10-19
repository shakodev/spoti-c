#include "ui/artwork.h"
#include "support/test_check.h"

/* Explicit public-network opt-in. These are the pinned dependency bytes, not
 * Spotify account traffic. Test both a small body and a body beyond the cap. */
static unsigned char body[ART_COMPRESSED_BYTES];
int main(void)
{
    Metrics metrics;
    CHECK(metrics_init(&metrics));
    CancelToken cancel = {.deadline = CORE_CLOCK_NEVER};
    size_t bytes = 0;
    CHECK(artwork_http_fetch(&metrics,
        L"https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/LICENSE",
        body, sizeof(body), &bytes, &cancel) == ART_OK);
    CHECK(bytes == 2510 && metrics_balanced(&metrics));
    CHECK(artwork_http_fetch(&metrics,
        L"https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_image.h",
        body, sizeof(body), &bytes, &cancel) == ART_TOO_LARGE);
    CHECK(bytes == 0 && metrics_balanced(&metrics));
    MetricsSnapshot stats = metrics_snapshot(&metrics);
    CHECK(stats.handles == 0 && stats.handle_high_water == 3 && stats.errors == 0);
    puts("artwork HTTPS: small/oversized pinned bodies and balanced WinHTTP handles passed");
    return 0;
}
