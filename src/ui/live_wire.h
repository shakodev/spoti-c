#ifndef SPOTIC_LIVE_WIRE_H
#define SPOTIC_LIVE_WIRE_H
#include "api/metadata_internal.h"
#define LIVE_ROWS 64u
#define LIVE_IMAGE_BYTES 1024u
#define LIVE_IMAGE_CANDIDATES 8u
#define LIVE_IMAGE_TARGET_BYTES 64u
/* Exact supplied PictureSize.url, never a synthesized image identifier.
 * Owned until projection serialization; the immutable JSON page then owns it. */
typedef struct LiveRow { char uri[192], name[192], image_uri[LIVE_IMAGE_BYTES]; } LiveRow;
typedef struct LiveList {
    LiveRow rows[LIVE_ROWS];
    uint32_t count, total, position;
    char name[192], revision[65];
    bool contents, truncated;
} LiveList;
bool live_list_decode(const void *, size_t, LiveList *);
bool live_url_component(const char *, wchar_t *, size_t);
#endif
