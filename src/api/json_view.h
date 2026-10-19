#ifndef SPOTIC_JSON_VIEW_H
#define SPOTIC_JSON_VIEW_H
#include "json_base.h"
#include <stdatomic.h>

#define JSON_PAGE_BODY_CAP ((size_t)80 * 1024)
#define JSON_PAGE_POOL_CAP ((size_t)384 * 1024)
typedef struct JsonPage {
    char *input;
    yyjson_doc *doc;
    uint64_t generation;
    atomic_uint refcount;
} JsonPage;

/* Owner reserves with refcount UINT_MAX, parses, then publishes refcount 1.
 * Retainers must already hold a live reference (or receive one by a synchronized
 * queue). Never retain through an unowned pointer; no weak references. */
JsonResult json_page_parse(JsonPage *page, size_t bytes, void *pool, size_t pool_bytes);
bool page_retain(JsonPage *page);
bool page_release(JsonPage *page);
JsonResult json_view_string(yyjson_val *object, const char *key, JsonString *output);
JsonResult json_view_uint(yyjson_val *object, const char *key, uint32_t *output);
bool json_span_equal(JsonString a, JsonString b);
#endif
