#include "collection_test.h"
#include "core/memory_budget.h"
#include <stdalign.h>

static alignas(16) unsigned char storage[COLLECTION_MODEL_BUDGET], pool_storage[MEMORY_BUDGET_API_PAGES];
CollectionService *collection_test_service;

CollectionRequest collection_test_fetch(CollectionKind kind, uint32_t offset, uint64_t generation)
{
    CollectionRequest request;
    CHECK(collection_fetch(collection_test_service, kind, offset, generation, &request) == COLLECTION_PENDING);
    CHECK(request.body != NULL && request.capacity == JSON_PAGE_BODY_CAP);
    CHECK(request.contract != NULL && request.generation == generation && request.cursor == offset);
    return request;
}

CollectionResult collection_test_deliver(CollectionRequest request, const char *json, const CollectionPage **page)
{
    size_t bytes = strlen(json); CHECK(bytes <= request.capacity);
    memcpy(request.body, json, bytes);
    return collection_complete(collection_test_service, request.ticket, COLLECTION_OK, 200, bytes, page);
}

CollectionResult collection_test_fixture(CollectionRequest request, const char *name, const CollectionPage **page)
{
    char path[512];
    CHECK(snprintf(path, sizeof(path), "%s/%s.json", API_FIXTURE_DIR, name) > 0);
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL); CHECK(doc != NULL);
    yyjson_val *response = yyjson_obj_get(yyjson_doc_get_root(doc), "response");
    yyjson_val *body = yyjson_obj_get(response, "body");
    unsigned status = body != NULL ? (unsigned)yyjson_get_uint(yyjson_obj_get(response, "status")) : 200;
    size_t bytes = 0; char *text = yyjson_val_write(body != NULL ? body : response, 0, &bytes);
    CHECK(text != NULL && bytes <= request.capacity);
    memcpy(request.body, text, bytes); free(text); yyjson_doc_free(doc);
    return collection_complete(collection_test_service, request.ticket, COLLECTION_OK, status, bytes, page);
}

static void predecessor_fixtures(void)
{
    const CollectionPage *page;
    CollectionRequest r = collection_test_fetch(COLLECTION_HOME, 0, 1);
    CHECK(r.contract->transport == API_PATHFINDER);
    CHECK(collection_test_fixture(r, "home-observed-positive", &page) == COLLECTION_OK);
    CHECK(page->count == 24 && page->next == COLLECTION_END && page->items[0].kind == COLLECTION_SHELF);
    printf("Home fixture DOM: %zu values, %zu value bytes\n", yyjson_doc_get_val_count(page->json.doc),
        yyjson_doc_get_val_count(page->json.doc) * sizeof(yyjson_val));
    CollectionItem item;
    CHECK(collection_shelf_item(page, 0, 0, &item) == COLLECTION_OK && item.source != NULL);
    CHECK(collection_shelf_item(page, 999, 0, &item) == COLLECTION_INVALID);
    collection_test_release(page);
    r = collection_test_fetch(COLLECTION_HOME_SECTION, 0, 2);
    CHECK(collection_test_fixture(r, "home_section-observed-positive", &page) == COLLECTION_OK);
    CHECK(page->count == 20 && page->next == 20);
    collection_test_release(page);
    r = collection_test_fetch(COLLECTION_HOME_SECTION, 20, 2);
    CHECK(collection_test_fixture(r, "home_section-observed-pagination", &page) == COLLECTION_BOUNDARY);
    CHECK(page->boundary && page->next == COLLECTION_END && page->count == 20);
    collection_test_release(page);
    r = collection_test_fetch(COLLECTION_HOME_SECTION, 0, 3);
    CHECK(collection_test_fixture(r, "home_section-observed-error", &page) == COLLECTION_ERROR && page == NULL);
    r = collection_test_fetch(COLLECTION_LIBRARY, 0, 4);
    CHECK(r.contract->transport == API_NATIVE_RPC && r.contract->url == NULL);
    CHECK(collection_test_fixture(r, "native-library-observed-positive", &page) == COLLECTION_OK);
    CHECK(page->count == 2 && page->total == 22 && page->next == 2 && page->items[0].pinned);
    collection_test_release(page);
    r = collection_test_fetch(COLLECTION_LIBRARY, 2, 4);
    CHECK(collection_test_fixture(r, "native-library-observed-pagination", &page) == COLLECTION_OK);
    CHECK(page->offset == 2 && page->next == 4);
    collection_test_release(page);
    r = collection_test_fetch(COLLECTION_LIBRARY, 0, 5);
    CHECK(collection_test_fixture(r, "native-library-observed-error", &page) == COLLECTION_ERROR && page == NULL);
    r = collection_test_fetch(COLLECTION_PLAYLIST, 0, 6);
    CHECK(r.contract->transport == API_NATIVE_RPC);
    CHECK(collection_test_fixture(r, "native-playlist-observed-positive", &page) == COLLECTION_OK);
    CHECK(page->count == 2 && page->next == 2 && page->metadata != NULL);
    CHECK(page->items[0].kind == COLLECTION_UNAVAILABLE && !page->items[0].available);
    collection_test_release(page);
    r = collection_test_fetch(COLLECTION_PLAYLIST, 2, 6);
    CHECK(collection_test_fixture(r, "native-playlist-observed-pagination", &page) == COLLECTION_OK);
    CHECK(page->offset == 2 && page->count == 2); collection_test_release(page);
    r = collection_test_fetch(COLLECTION_PLAYLIST, 0, 7);
    CHECK(collection_test_fixture(r, "native-playlist-observed-error", &page) == COLLECTION_ERROR);
    CHECK(collection_reset(collection_test_service, 8) == COLLECTION_OK);
    CHECK(collection_resident_pages(collection_test_service) == 0);
}

static const char library_page[] = "{\"statusCode\":200,\"header\":{\"totalCount\":10000},\"entity\":["
    "{\"entityInfo\":{\"name\":\"Bj\\u00f6rk \\ud83c\\udfb5\",\"uri\":\"folder:1\"},\"folder\":{}},"
    "{\"entityInfo\":{\"uri\":\"folder:1\"},\"folder\":{}},null,{\"entityInfo\":{\"uri\":\"future:1\"},\"future\":{}}],"
    "\"pinnedEntity\":[{\"entityInfo\":{\"uri\":\"pin:1\"}}]}";

static void lifetimes_and_unions(void)
{
    const CollectionPage *first, *second, *none;
    CollectionRequest r = collection_test_fetch(COLLECTION_LIBRARY, 0, 10);
    CHECK(collection_test_deliver(r, library_page, &first) == COLLECTION_OK);
    CHECK(first->items[0].kind == COLLECTION_FOLDER && first->items[1].duplicate);
    CHECK(first->items[2].kind == COLLECTION_UNAVAILABLE && first->items[3].kind == COLLECTION_UNKNOWN);
    CHECK(first->pinned_count == 1 && first->count == 4 && first->next == 4);
    CHECK(first->items[0].name.bytes == 11); /* UTF-8: Bjork with umlaut + music symbol. */
    CHECK(page_retain((JsonPage *)&first->json));
    r = collection_test_fetch(COLLECTION_LIBRARY, 4, 10);
    CHECK(collection_test_deliver(r, library_page, &second) == COLLECTION_OK);
    CHECK(second->items[0].duplicate && collection_resident_pages(collection_test_service) == 2);
    CollectionRequest full;
    CHECK(collection_fetch(collection_test_service, COLLECTION_LIBRARY, 8, 10, &full) == COLLECTION_FULL);
    CHECK(first->items[0].name.data[0] == 'B');
    collection_test_release(first); collection_test_release(first);
    CHECK(collection_resident_pages(collection_test_service) == 1);
    CHECK(!page_release((JsonPage *)&first->json) && !page_retain((JsonPage *)&first->json));
    collection_test_release(second);
    r = collection_test_fetch(COLLECTION_LIBRARY, 8, 10);
    CHECK(collection_test_deliver(r, "{\"statusCode\":200,\"header\":{\"totalCount\":8},\"entity\":[]}", &none) == COLLECTION_OK);
    CHECK(none->next == COLLECTION_END); collection_test_release(none);
    CHECK(collection_reset(collection_test_service, 11) == COLLECTION_OK);
    CHECK(collection_resident_pages(collection_test_service) == 0);
    const char *liked_uri = "adapter:liked-for-account";
    CHECK(collection_configure(collection_test_service, 2,
        (JsonString){liked_uri, strlen(liked_uri)}) == COLLECTION_OK);
    r = collection_test_fetch(COLLECTION_LIBRARY, 0, 12);
    CHECK(collection_test_deliver(r, "{\"statusCode\":200,\"header\":{\"totalCount\":1},\"entity\":["
        "{\"entityInfo\":{\"uri\":\"adapter:liked-for-account\"},\"playlist\":{\"canView\":true}}]}", &first) == COLLECTION_OK);
    CHECK(first->items[0].kind == COLLECTION_LIKED && first->items[0].available);
    CHECK(collection_configure(collection_test_service, 4, (JsonString){0}) == COLLECTION_FULL);
    collection_test_release(first);
    CHECK(collection_reset(collection_test_service, 13) == COLLECTION_OK);
}

static void repeated_navigation(void)
{
    for (uint64_t gen = 100; gen < 1100; ++gen) {
        const CollectionPage *page;
        CollectionRequest r = collection_test_fetch(COLLECTION_LIBRARY, 0, gen);
        CHECK(collection_test_deliver(r, library_page, &page) == COLLECTION_OK);
        CHECK(atomic_load(&page->json.refcount) == 2);
        collection_test_release(page);
        CHECK(collection_reset(collection_test_service, gen + 1) == COLLECTION_OK);
        CHECK(collection_resident_pages(collection_test_service) == 0);
    }
}

static void large_playlist(void)
{
    CHECK(collection_configure(collection_test_service, 64, (JsonString){0}) == COLLECTION_OK);
    uint32_t offset = 0, pages = 0;
    while (offset < 10000) {
        CollectionRequest r = collection_test_fetch(COLLECTION_PLAYLIST, offset, 2000);
        size_t bytes = (size_t)snprintf(r.body, r.capacity,
            "{\"status\":{\"statusCode\":200},\"query\":{\"range\":{\"start\":%u,\"length\":64}},"
            "\"data\":{\"contentSignature\":\"stable\",\"unrangedLength\":10000,\"item\":[", offset);
        uint32_t count = 10000 - offset < 64 ? 10000 - offset : 64;
        for (uint32_t i = 0; i < count; ++i) {
            int n = snprintf(r.body + bytes, r.capacity - bytes,
                "%s{\"rowId\":\"row:%u\",\"uri\":\"same-track\",\"trackMetadata\":{}}", i ? "," : "", offset+i);
            CHECK(n > 0 && (size_t)n < r.capacity - bytes); bytes += (size_t)n;
        }
        CHECK(bytes + 3 <= r.capacity); memcpy(r.body + bytes, "]}}", 3); bytes += 3;
        const CollectionPage *page;
        CHECK(collection_complete(collection_test_service, r.ticket, COLLECTION_OK, 200, bytes, &page) == COLLECTION_OK);
        CHECK(page->count == count && page->total == 10000);
        for (uint32_t i = 0; i < count; ++i) { CHECK(!page->items[i].duplicate); }
        offset += count; ++pages;
        CHECK(page->next == (offset == 10000 ? COLLECTION_END : offset));
        collection_test_release(page);
        CHECK(collection_resident_pages(collection_test_service) == 1);
    }
    CHECK(pages == 157 && collection_reset(collection_test_service, 2001) == COLLECTION_OK);
    CHECK(collection_resident_pages(collection_test_service) == 0);
}

int main(void)
{
    CHECK(collection_service_size() <= sizeof(storage));
    CHECK(collection_service_alignment() <= 16);
    JsonPagePool *pool;
    CHECK(json_page_pool_init(pool_storage, sizeof(pool_storage), &pool) == JSON_OK);
    CHECK(collection_service_init(storage, sizeof(storage), pool, &collection_test_service) == COLLECTION_OK);
    CHECK(collection_configure(collection_test_service, 2, (JsonString){0}) == COLLECTION_OK);
    predecessor_fixtures(); lifetimes_and_unions(); collection_error_tests(); collection_race_tests(); repeated_navigation();
    large_playlist();
    printf("Collection fixtures, bounded DOM pools, generation and lifetime tests passed (%zu bytes)\n", collection_service_size());
    return 0;
}
