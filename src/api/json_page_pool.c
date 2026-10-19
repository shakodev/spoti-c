#include "json_view.h"
#include "core/memory_budget.h"
#include <limits.h>
#include <stdalign.h>
#include <string.h>
#if defined(SPOTIC_ASAN)
#include <sanitizer/asan_interface.h>
#define POISON(p,n) __asan_poison_memory_region((p),(n))
#define UNPOISON(p,n) __asan_unpoison_memory_region((p),(n))
#else
#define POISON(p,n) ((void)(p),(void)(n))
#define UNPOISON(p,n) ((void)(p),(void)(n))
#endif

typedef struct JsonPoolSlot {
    atomic_uint busy;
    uint64_t pool[JSON_PAGE_POOL_CAP / sizeof(uint64_t)];
    char input[JSON_PAGE_BODY_CAP + YYJSON_PADDING_SIZE];
} JsonPoolSlot;
struct JsonPagePool { JsonPoolSlot slots[2]; };
_Static_assert(sizeof(JsonPagePool) <= MEMORY_BUDGET_API_PAGES, "Single shared API page budget");

size_t json_page_pool_size(void) { return sizeof(JsonPagePool); }
size_t json_page_pool_alignment(void) { return alignof(JsonPagePool); }
JsonResult json_page_pool_init(void *storage, size_t bytes, JsonPagePool **output)
{
    if (output == NULL) { return JSON_INVALID; }
    *output = NULL;
    if (storage == NULL || bytes < sizeof(JsonPagePool) ||
        (uintptr_t)storage % alignof(JsonPagePool) != 0) { return JSON_INVALID; }
    JsonPagePool *pool = storage;
    memset(pool, 0, sizeof(*pool));
    for (size_t i = 0; i < 2; ++i) {
        atomic_init(&pool->slots[i].busy, 0);
        POISON(pool->slots[i].pool, JSON_PAGE_POOL_CAP);
        POISON(pool->slots[i].input, sizeof(pool->slots[i].input));
    }
    *output = pool; return JSON_OK;
}

size_t json_page_pool_resident(const JsonPagePool *pool)
{
    size_t count = 0;
    if (pool != NULL) {
        for (size_t i = 0; i < 2; ++i) { count += atomic_load_explicit(&pool->slots[i].busy, memory_order_acquire) != 0; }
    }
    return count;
}
JsonResult json_page_pool_dispose(JsonPagePool *pool)
{
    if (pool==NULL) { return JSON_INVALID; }
    if (json_page_pool_resident(pool)!=0) { return JSON_INVALID; }
    UNPOISON(pool,sizeof(*pool)); memset(pool,0,sizeof(*pool)); return JSON_OK;
}

JsonResult json_page_acquire(JsonPagePool *pool, JsonPage *page, uint64_t generation)
{
    if (pool == NULL || page == NULL || page->reservation != NULL ||
        atomic_load(&page->refcount) != UINT_MAX) { return JSON_INVALID; }
    for (size_t i = 0; i < 2; ++i) {
        JsonPoolSlot *slot = &pool->slots[i]; unsigned expected = 0;
        if (!atomic_compare_exchange_strong_explicit(&slot->busy, &expected, 1,
            memory_order_acq_rel, memory_order_acquire)) { continue; }
        UNPOISON(slot->pool, JSON_PAGE_POOL_CAP); UNPOISON(slot->input, sizeof(slot->input));
        page->input = slot->input; page->pool = slot->pool; page->reservation = &slot->busy;
        page->doc = NULL; page->generation = generation; return JSON_OK;
    }
    return JSON_NO_MEMORY;
}

void json_page_discard(JsonPage *page)
{
    if (page == NULL || atomic_load(&page->refcount) != UINT_MAX) { return; }
    atomic_uint *reservation = page->reservation;
    yyjson_doc_free(page->doc); page->doc = NULL;
    if (reservation != NULL) {
        POISON(page->input, JSON_PAGE_BODY_CAP + YYJSON_PADDING_SIZE);
        POISON(page->pool, JSON_PAGE_POOL_CAP);
    }
    page->input = NULL; page->pool = NULL; page->reservation = NULL;
    atomic_store_explicit(&page->refcount, 0, memory_order_release);
    if (reservation != NULL) { atomic_store_explicit(reservation, 0, memory_order_release); }
}
