#ifndef SPOTIC_CATALOG_INTERNAL_H
#define SPOTIC_CATALOG_INTERNAL_H
#include "catalog_api.h"
#include <limits.h>
#include <stdalign.h>
#define KGET yyjson_obj_get
typedef enum CatalogFamily { CATALOG_SEARCH_FAMILY, CATALOG_ALBUM_FAMILY, CATALOG_ARTIST_FAMILY } CatalogFamily;
typedef struct CatalogSlot {
    CatalogPage page;
    uint64_t serial;
    uint32_t selector;
    bool pending, cancelled;
    char key[CATALOG_KEY_CAP + 1], upload[API_CONTRACT_REQUEST_CAP];
    size_t upload_bytes;
} CatalogSlot;
struct CatalogService {
    JsonPagePool *pool;
    CatalogConfig config;
    char locale[64], key[CATALOG_KEY_CAP + 1];
    size_t key_bytes;
    uint64_t generation, serial, due_ms;
    CatalogFamily family;
    uint32_t selector, limit, next;
    bool has_context, cancelled, debouncing;
    CatalogPage *current;
    CatalogSlot slots[2];
};
CatalogResult catalog_prepare(CatalogService *s, ApiOperation operation, CatalogFamily family,
    JsonString key, uint32_t selector, uint32_t offset, uint32_t limit,
    uint64_t generation, uint64_t now_ms, CatalogRequest *output);
CatalogResult catalog_serialize(CatalogService *s, CatalogSlot *slot);
CatalogResult catalog_parse_search(CatalogPage *page, uint32_t selector, uint32_t width, yyjson_val *root);
CatalogResult catalog_parse_album(CatalogPage *page, uint32_t width, yyjson_val *root);
CatalogResult catalog_parse_artist(CatalogPage *page, uint32_t selector, uint32_t width, yyjson_val *root);
JsonString catalog_string(yyjson_val *object, const char *key);
bool catalog_type(yyjson_val *object, const char *type);
CatalogId catalog_compact_id(JsonString uri, CatalogKind kind);
CatalogResult catalog_union(yyjson_val *value, const char *expected);
CatalogResult catalog_read_item(yyjson_val *data, CatalogKind kind, uint32_t width, CatalogItem *item);
CatalogResult catalog_read_wrapper(yyjson_val *wrapper, uint32_t width, CatalogItem *item);
CatalogResult catalog_read_rows(CatalogPage *page, yyjson_val *rows, uint32_t width, bool tracks);
CatalogResult catalog_paging(CatalogPage *page, yyjson_val *container, bool has_total);
#endif
