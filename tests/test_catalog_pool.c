#include "catalog_test.h"
#include "core/memory_budget.h"
#if defined(SPOTIC_ASAN)
#include <sanitizer/asan_interface.h>
#endif

static void shared_admission_and_lifetime(void)
{
    CollectionRequest collection;
    CHECK(collection_fetch(catalog_test_collections, COLLECTION_LIBRARY, 0, 1, &collection) == COLLECTION_PENDING);
    const char *json = "{\"statusCode\":200,\"header\":{\"totalCount\":2},\"entity\":[{\"entityInfo\":{\"uri\":\"held\"},\"album\":{}}]}";
    memcpy(collection.body, json, strlen(json)); const CollectionPage *held;
    CHECK(collection_complete(catalog_test_collections, collection.ticket, COLLECTION_OK, 200, strlen(json), &held) == COLLECTION_OK);
    CatalogRequest catalog = catalog_test_album(0, 200);
    CHECK(json_page_pool_resident(catalog_test_pool) == 2);
    CollectionRequest rejected;
    CHECK(collection_fetch(catalog_test_collections, COLLECTION_LIBRARY, 1, 1, &rejected) == COLLECTION_FULL);
    const CatalogPage *page;
    CHECK(catalog_test_fixture(catalog, "album-observed-positive", &page) == CATALOG_OK);
    const char *name = page->detail.name.data;
    CHECK(name != NULL && page_retain((JsonPage *)&page->json));
    CHECK(catalog_reset(catalog_test_service, 201) == CATALOG_OK);
    catalog_test_release(page);
    CHECK(memcmp(name, "redacted", 8) == 0);
#if defined(SPOTIC_ASAN)
    CHECK(!__asan_address_is_poisoned(name));
#endif
    catalog_test_release(page);
#if defined(SPOTIC_ASAN)
    CHECK(__asan_address_is_poisoned(name));
#endif
    CHECK(json_page_pool_resident(catalog_test_pool) == 1);
    CHECK(collection_fetch(catalog_test_collections, COLLECTION_LIBRARY, 1, 1, &collection) == COLLECTION_PENDING);
    const CollectionPage *none;
    CHECK(collection_complete(catalog_test_collections, collection.ticket, COLLECTION_CANCELLED, 0, 0, &none) == COLLECTION_CANCELLED);
    CHECK(page_release((JsonPage *)&held->json));
    CHECK(collection_reset(catalog_test_collections, 2) == COLLECTION_OK);
    CHECK(json_page_pool_resident(catalog_test_pool) == 0);
}

static void cap_and_navigation(void)
{
    CatalogRequest r = catalog_test_search("x", SEARCH_TRACKS, 0, 100, 210);
    size_t bytes = (size_t)snprintf(r.body, r.capacity, "{\"data\":{\"searchV2\":{\"tracksV2\":{\"totalCount\":101,\"items\":[");
    for (unsigned i = 0; i < 101; ++i) {
        const char *text = i ? ",null" : "null"; size_t n = strlen(text);
        memcpy(r.body + bytes, text, n); bytes += n;
    }
    memcpy(r.body + bytes, "]}}}}", 5); bytes += 5;
    const CatalogPage *page;
    CHECK(catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, 200, bytes,
        span(r.contract->persisted_sha256), &page) == CATALOG_TOO_LARGE);
    CHECK(json_page_pool_resident(catalog_test_pool) == 0);
    r = catalog_test_search("images", SEARCH_TRACKS, 0, 2, 211);
    bytes = (size_t)snprintf(r.body, r.capacity, "{\"data\":{\"searchV2\":{\"tracksV2\":{\"totalCount\":1,\"items\":[{\"item\":{\"__typename\":\"TrackResponseWrapper\",\"data\":{"
        "\"__typename\":\"Track\",\"albumOfTrack\":{\"coverArt\":{\"sources\":[");
    for (unsigned i = 0; i < 33; ++i) {
        const char *text = i ? ",{\"url\":\"image\"}" : "{\"url\":\"image\"}";
        size_t n = strlen(text); memcpy(r.body + bytes, text, n); bytes += n;
    }
    const char *end = "]}}}}}]}}}}"; memcpy(r.body + bytes, end, strlen(end)); bytes += strlen(end);
    CHECK(catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, 200, bytes,
        span(r.contract->persisted_sha256), &page) == CATALOG_TOO_LARGE);
    CHECK(json_page_pool_resident(catalog_test_pool) == 0);
    r = catalog_test_search("borrowed", SEARCH_ALBUMS, 0, 2, 212);
    CHECK(catalog_test_deliver(r, "{\"data\":{\"searchV2\":{\"albumsV2\":{\"totalCount\":1,\"items\":[{\"__typename\":\"AlbumResponseWrapper\",\"data\":{"
        "\"__typename\":\"Album\",\"uri\":\"spotify:album:0000000000000000000001\",\"playability\":{\"playable\":true}}}]}}}}", &page) == CATALOG_OK);
    JsonString borrowed = page->items[0].uri;
    catalog_test_release(page); /* Service now holds the sole reference. */
    CHECK(album_request(catalog_test_service, borrowed, 0, 213, &r) == CATALOG_PENDING);
    CHECK(catalog_complete(catalog_test_service, r.ticket, CATALOG_CANCELLED, 0, 0, (JsonString){0}, &page) == CATALOG_CANCELLED);
    CHECK(json_page_pool_resident(catalog_test_pool) == 0);
    for (uint64_t gen = 300; gen < 1300; ++gen) {
        r = catalog_test_album(0, gen);
        CHECK(catalog_test_deliver(r, "{\"data\":{\"albumUnion\":{\"__typename\":\"Album\",\"tracksV2\":{\"totalCount\":0,\"items\":[]}}}}", &page) == CATALOG_OK);
        catalog_test_release(page);
        CHECK(catalog_reset(catalog_test_service, gen + 1) == CATALOG_OK);
        CHECK(json_page_pool_resident(catalog_test_pool) == 0);
    }
}

void catalog_pool_tests(void)
{
    CHECK(json_page_pool_size() <= MEMORY_BUDGET_API_PAGES);
    CHECK(catalog_service_size() + collection_service_size() <= MEMORY_BUDGET_MODEL);
    shared_admission_and_lifetime(); cap_and_navigation();
}
