#ifndef SPOTIC_COLLECTION_INTERNAL_H
#define SPOTIC_COLLECTION_INTERNAL_H
#include "collection_api.h"
#include <stdalign.h>
#include <limits.h>
#define CGET yyjson_obj_get
typedef struct CollectionSlot {
    CollectionPage page;
    uint64_t serial;
    bool pending, cancelled;
    uint64_t pool[JSON_PAGE_POOL_CAP / sizeof(uint64_t)];
    char input[JSON_PAGE_BODY_CAP + YYJSON_PADDING_SIZE];
} CollectionSlot;
struct CollectionService {
    uint64_t generation, serial;
    uint32_t next;
    uint32_t limit;
    char liked_uri[256];
    size_t liked_uri_bytes;
    CollectionKind kind;
    CollectionPage *current;
    CollectionSlot slots[2];
};
CollectionResult collection_home_parse(CollectionPage *page, yyjson_val *root);
CollectionResult collection_library_parse(CollectionPage *page, yyjson_val *root, JsonString liked_uri);
CollectionResult collection_playlist_parse(CollectionPage *page, yyjson_val *root, uint32_t limit);
CollectionResult collection_rows(CollectionPage *page, yyjson_val *array);
CollectionItem collection_home_item(yyjson_val *value);
void collection_compact_id(CollectionItem *item);
JsonString collection_string(yyjson_val *object, const char *key);
CollectionResult collection_status(unsigned status);
#endif
