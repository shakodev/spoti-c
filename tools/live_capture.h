#ifndef SPOTIC_LIVE_CAPTURE_H
#define SPOTIC_LIVE_CAPTURE_H
#include "ui/app.h"

/* Explicit, synchronous opt-in for the read-only account probe. Caller retains
 * its page reference and backing pool/service throughout this call. The helper
 * retains separately and transfers that reference through app_deliver.
 * directory must be a LOCAL ignored artifact directory (e.g. build/live-captures).
 * Captured pixels contain account-visible labels/artwork; stdout/JSON contain
 * only capture identity, dimensions, counts and hashes, never item names/URLs.
 * No window automation, account mutations, audio or replacement fixtures.
 * One caller/thread at a time. False can mean timeout/incomplete capture. */
bool live_capture_page(const CollectionPage *page, AppRequest request,
    CollectionResult result, const char *notice, const char *directory, const char *name, bool grid);
/* Retry after a reported unbalanced cleanup before dependencies/process exit.
 * Outstanding worker storage is static and never freed beneath callbacks. */
bool live_capture_shutdown(DWORD timeout_ms);
#endif
