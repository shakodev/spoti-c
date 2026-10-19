#ifndef SPOTIC_COLLECTION_TEST_H
#define SPOTIC_COLLECTION_TEST_H
#include "api/collection_api.h"
#include "support/test_check.h"
#include <string.h>

extern CollectionService *collection_test_service;
CollectionRequest collection_test_fetch(CollectionKind kind, uint32_t offset, uint64_t generation);
CollectionResult collection_test_deliver(CollectionRequest request, const char *json,
    const CollectionPage **page);
CollectionResult collection_test_fixture(CollectionRequest request, const char *name,
    const CollectionPage **page);
void collection_error_tests(void);
void collection_race_tests(void);
static inline void collection_test_release(const CollectionPage *page)
{
    CHECK(page != NULL && page_release((JsonPage *)&page->json));
}
#endif
