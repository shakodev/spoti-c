#ifndef SPOTIC_CATALOG_TEST_H
#define SPOTIC_CATALOG_TEST_H
#include "api/catalog_api.h"
#include "api/collection_api.h"
#include "support/test_check.h"
#include <string.h>
extern CatalogService *catalog_test_service;
extern CollectionService *catalog_test_collections;
extern JsonPagePool *catalog_test_pool;
extern const char catalog_album_uri[], catalog_artist_uri[];
static inline JsonString span(const char *text) { return (JsonString){text, strlen(text)}; }
CatalogRequest catalog_test_search(const char *query, SearchCategory category,
    uint32_t offset, uint32_t limit, uint64_t generation);
CatalogRequest catalog_test_album(uint32_t offset, uint64_t generation);
CatalogRequest catalog_test_artist(ArtistSection section, uint64_t generation);
CatalogResult catalog_test_deliver(CatalogRequest request, const char *json, const CatalogPage **page);
CatalogResult catalog_test_fixture(CatalogRequest request, const char *name, const CatalogPage **page);
void catalog_error_tests(void);
void catalog_pool_tests(void);
static inline void catalog_test_release(const CatalogPage *page)
{
    CHECK(page != NULL && page_release((JsonPage *)&page->json));
}
#endif
