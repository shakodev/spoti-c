#include "json_view.h"
#include <limits.h>
#include <string.h>

JsonResult json_page_parse(JsonPage *page, size_t bytes, void *pool, size_t pool_bytes)
{
    yyjson_alc allocator;
    yyjson_read_err error;
    if (page == NULL || page->input == NULL || page->doc != NULL || pool == NULL ||
        atomic_load(&page->refcount) != UINT_MAX) { return JSON_INVALID; }
    if (bytes > JSON_PAGE_BODY_CAP || pool_bytes > JSON_PAGE_POOL_CAP) { return JSON_TOO_LARGE; }
    uintptr_t a = (uintptr_t)page->input, b = (uintptr_t)pool;
    size_t capacity = JSON_PAGE_BODY_CAP + YYJSON_PADDING_SIZE;
    if (capacity > UINTPTR_MAX - a || pool_bytes > UINTPTR_MAX - b ||
        (a < b + pool_bytes && b < a + capacity)) { return JSON_INVALID; }
    if (!yyjson_alc_pool_init(&allocator, pool, pool_bytes)) { return JSON_NO_MEMORY; }
    memset(page->input + bytes, 0, YYJSON_PADDING_SIZE);
    page->doc = yyjson_read_opts(page->input, bytes, YYJSON_READ_INSITU, &allocator, &error);
    if (page->doc == NULL) {
        return error.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? JSON_NO_MEMORY : JSON_INVALID;
    }
    return JSON_OK;
}

bool page_retain(JsonPage *page)
{
    if (page == NULL) { return false; }
    unsigned count = atomic_load_explicit(&page->refcount, memory_order_acquire);
    while (count != 0 && count < UINT_MAX - 1) {
        if (atomic_compare_exchange_weak_explicit(&page->refcount, &count, count + 1,
            memory_order_acq_rel, memory_order_acquire)) { return true; }
    }
    return false;
}

bool page_release(JsonPage *page)
{
    if (page == NULL) { return false; }
    unsigned count = atomic_load_explicit(&page->refcount, memory_order_acquire);
    while (count != 0 && count != UINT_MAX) {
        unsigned next = count == 1 ? UINT_MAX : count - 1;
        if (atomic_compare_exchange_weak_explicit(&page->refcount, &count, next,
            memory_order_acq_rel, memory_order_acquire)) {
            if (next == UINT_MAX) {
                yyjson_doc_free(page->doc); page->doc = NULL;
                atomic_store_explicit(&page->refcount, 0, memory_order_release);
            }
            return true;
        }
    }
    return false;
}

JsonResult json_view_string(yyjson_val *object, const char *key, JsonString *output)
{
    yyjson_val *v = NULL;
    JsonResult result = json_base_field(object, key, &v);
    if (output != NULL) { *output = (JsonString){0}; }
    return result == JSON_OK ? json_base_string(v, 4096, output) : result;
}

JsonResult json_view_uint(yyjson_val *object, const char *key, uint32_t *output)
{
    yyjson_val *v = NULL; uint64_t number = 0;
    if (output == NULL) { return JSON_INVALID; }
    *output = 0;
    JsonResult result = json_base_field(object, key, &v);
    if (result == JSON_OK) { result = json_base_u64(v, UINT32_MAX, &number); }
    if (result == JSON_OK) { *output = (uint32_t)number; }
    return result;
}

bool json_span_equal(JsonString a, JsonString b)
{
    return a.bytes == b.bytes && (a.bytes == 0 ||
        (a.data != NULL && b.data != NULL && memcmp(a.data, b.data, a.bytes) == 0));
}
