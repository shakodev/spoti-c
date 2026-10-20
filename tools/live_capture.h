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
typedef struct LiveCaptureViewport { uint32_t width,height,dpi; } LiveCaptureViewport;
/* Width/height are DIP. Bounded diagnostic viewports: 400..1800 by 320..900,
 * 96..192 DPI. Exact physical readback is allocated once outside frames and
 * capped at 3600*1800*4 bytes; captures are never resampled. sidebar is optional
 * and borrowed from an already-owned live projection throughout this call. */
bool live_capture_page_at(const CollectionPage *page,AppRequest request,
    CollectionResult result,const char *notice,const char *directory,const char *name,bool grid,
    LiveCaptureViewport viewport,const AppRootlistPage *sidebar);
/* 400/1100/1800 DIP by 900 DIP at 96/120/144/192 DPI. Home is one view;
 * Library captures list and grid from the same owned immutable live page. */
bool live_capture_matrix(const CollectionPage *page,AppRequest request,
    CollectionResult result,const char *notice,const char *directory,const char *name,
    const AppRootlistPage *sidebar);
/* Retry after a reported unbalanced cleanup before dependencies/process exit.
 * Outstanding worker storage is static and never freed beneath callbacks. */
bool live_capture_shutdown(DWORD timeout_ms);
#endif
