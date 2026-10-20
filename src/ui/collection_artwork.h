#ifndef SPOTIC_UI_COLLECTION_ARTWORK_H
#define SPOTIC_UI_COLLECTION_ARTWORK_H
#include "artwork.h"
#include "app_result.h"
#include "api/collection_api.h"

#define COLLECTION_ARTWORK_SOURCES 8u
typedef struct CollectionArtwork {
    ArtworkSource sources[COLLECTION_ARTWORK_SOURCES];
    wchar_t urls[COLLECTION_ARTWORK_SOURCES][ART_URL_CHARS];
    uint8_t content[20];
    uint32_t count;
} CollectionArtwork;
_Static_assert(sizeof(CollectionArtwork)<=17u*1024u,"bounded collection image sources");
struct App;

/* Preparation only: validates/copies retained JSON, then computes a CNG cache
 * identity over every exact source URL and dimensions. No network/GPU calls.
 * Output owns URLs; do not copy the struct because sources point into urls.
 * Missing/unsupported data is UNAVAILABLE; malformed data is INVALID; capacity
 * overflow is FULL. Every failure clears output, including partial variants. */
AppResult app_collection_artwork_read(const CollectionItem *item,CollectionArtwork *output);
/* Requires an initialized App outside layout/render. Existing app_image copies
 * admission data; no borrowed metadata or stack URL survives the return. */
AppResult app_collection_image(struct App *app,const CollectionItem *item,uint32_t requested_size);
#endif
