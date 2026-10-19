#ifndef SPOTIC_JSON_VIEW_H
#define SPOTIC_JSON_VIEW_H
#include "json_base.h"
#include <stdatomic.h>

#define JSON_PAGE_BODY_CAP ((size_t)140 * 1024)
#define JSON_PAGE_POOL_CAP ((size_t)368 * 1024)
typedef struct JsonPagePool JsonPagePool;
typedef struct JsonPage {
    char *input;
    yyjson_doc *doc;
    uint64_t generation;
    atomic_uint refcount;
    void *pool;
    atomic_uint *reservation;
} JsonPage;

/* Owner reserves with refcount UINT_MAX, parses, then publishes owned references.
 * Retainers must already hold a live reference (or receive one by a synchronized
 * queue). Never retain through an unowned pointer; no weak references. */
JsonResult json_page_parse(JsonPage *page, size_t bytes);
/* Allocate exactly ONE pool from MEMORY_BUDGET_API_PAGES and pass it to both
 * collections and catalog. Storage must outlive every reservation/reference.
 * Init only fresh storage; network owner reserves, final UI release retires. */
size_t json_page_pool_size(void);
size_t json_page_pool_alignment(void);
JsonResult json_page_pool_init(void *storage, size_t bytes, JsonPagePool **output);
size_t json_page_pool_resident(const JsonPagePool *pool);
JsonResult json_page_acquire(JsonPagePool *pool, JsonPage *page, uint64_t generation);
void json_page_discard(JsonPage *page); /* Exclusive UINT_MAX reservation only. */
/* Exclusive owner: release the temporary DOM and replace it with a compact
 * model. Source must be outside the pool; model spans may borrow input. The
 * materialized payload and input retain exactly the JsonPage lifetime. */
JsonResult json_page_materialize(JsonPage *page, const void *model, size_t bytes, void **output);
bool page_retain(JsonPage *page);
bool page_release(JsonPage *page);
JsonResult json_view_string(yyjson_val *object, const char *key, JsonString *output);
JsonResult json_view_uint(yyjson_val *object, const char *key, uint32_t *output);
bool json_span_equal(JsonString a, JsonString b);
#endif
