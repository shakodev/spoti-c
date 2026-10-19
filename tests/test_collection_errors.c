#include "collection_test.h"
#include <limits.h>

/* Fault injection exercises local policy. It is not server acceptance evidence. */
static const char playlist[] = "{\"status\":{\"statusCode\":200},\"query\":{\"range\":{\"start\":0,\"length\":2}},"
    "\"data\":{\"contentSignature\":\"r1\",\"unrangedLength\":3,\"item\":["
    "{\"rowId\":\"row1\",\"uri\":\"spotify:track:0000000000000000000001\",\"trackMetadata\":{\"name\":\"one\"},\"trackPlayState\":{\"isPlayable\":true}},"
    "{\"rowId\":\"row2\",\"uri\":\"spotify:track:0000000000000000000001\",\"trackMetadata\":{},\"trackPlayState\":{\"isPlayable\":false}}]}}";

static void generations(void)
{
    const CollectionPage *page;
    CollectionRequest old = collection_test_fetch(COLLECTION_HOME, 0, 20), request;
    CHECK(collection_fetch(collection_test_service, COLLECTION_HOME, 0, 20, &request) == COLLECTION_PENDING);
    CHECK(request.body == NULL); /* One in flight per generation. */
    CollectionRequest current = collection_test_fetch(COLLECTION_LIBRARY, 0, 21);
    CHECK(collection_resident_pages(collection_test_service) == 2);
    CHECK(collection_complete(collection_test_service, old.ticket, COLLECTION_OK, 200, 0, &page) == COLLECTION_STALE);
    CHECK(page == NULL && collection_resident_pages(collection_test_service) == 1);
    CHECK(collection_cancel(collection_test_service, current.ticket) == COLLECTION_PENDING);
    CHECK(collection_resident_pages(collection_test_service) == 1); /* Cannot reclaim a live transport buffer. */
    CHECK(collection_complete(collection_test_service, current.ticket, COLLECTION_OK, 200, 0, &page) == COLLECTION_CANCELLED);
    CHECK(collection_complete(collection_test_service, old.ticket, COLLECTION_OK, 200, 0, &page) == COLLECTION_STALE);
    CHECK(collection_resident_pages(collection_test_service) == 0);
    current = collection_test_fetch(COLLECTION_LIBRARY, 0, 21);
    CHECK(collection_cancel(collection_test_service, old.ticket) == COLLECTION_STALE);
    CHECK(collection_complete(collection_test_service, current.ticket, COLLECTION_CANCELLED, 0, 0, &page) == COLLECTION_CANCELLED);
    CHECK(collection_fetch(collection_test_service, COLLECTION_HOME, 0, 20, &request) == COLLECTION_STALE);
}

static void revisions_and_order(void)
{
    const CollectionPage *page;
    CollectionRequest r = collection_test_fetch(COLLECTION_PLAYLIST, 0, 30), rejected;
    CHECK(collection_test_deliver(r, playlist, &page) == COLLECTION_OK);
    CHECK(page->next == 2 && page->items[0].available && !page->items[1].available);
    CHECK(!page->items[1].duplicate); /* Repeated track URIs are legitimate distinct rows. */
    CHECK(page->items[0].has_compact_id && page->items[0].compact_id[15] == 1);
    for (size_t i = 0; i < 15; ++i) { CHECK(page->items[0].compact_id[i] == 0); }
    collection_test_release(page);
    CHECK(collection_fetch(collection_test_service, COLLECTION_PLAYLIST, 1, 30, &rejected) == COLLECTION_ORDER);
    CHECK(collection_fetch(collection_test_service, COLLECTION_PLAYLIST, 0, 30, &rejected) == COLLECTION_ORDER);
    CHECK(collection_fetch(collection_test_service, COLLECTION_LIBRARY, 2, 30, &rejected) == COLLECTION_ORDER);
    r = collection_test_fetch(COLLECTION_PLAYLIST, 2, 30);
    CHECK(collection_test_deliver(r, "{\"status\":{\"statusCode\":200},\"query\":{\"range\":{\"start\":2,\"length\":1}},"
        "\"data\":{\"contentSignature\":\"r1\",\"unrangedLength\":3,\"item\":[]}}", &page) == COLLECTION_ORDER);
    r = collection_test_fetch(COLLECTION_PLAYLIST, 2, 30);
    CHECK(collection_test_deliver(r, playlist, &page) == COLLECTION_ORDER && page == NULL);
    r = collection_test_fetch(COLLECTION_PLAYLIST, 2, 30);
    CHECK(collection_test_deliver(r, "{\"status\":{\"statusCode\":200},\"query\":{\"range\":{\"start\":2,\"length\":2}},"
        "\"data\":{\"contentSignature\":\"r2\",\"unrangedLength\":3,\"item\":[]}}", &page) == COLLECTION_REVISION);
    CHECK(page == NULL);
    r = collection_test_fetch(COLLECTION_PLAYLIST, 2, 30);
    CHECK(collection_test_deliver(r, "{\"status\":{\"statusCode\":200},\"query\":{\"range\":{\"start\":2,\"length\":2}},"
        "\"data\":{\"contentSignature\":\"r1\",\"unrangedLength\":3,\"item\":[null]}}", &page) == COLLECTION_OK);
    CHECK(page->next == COLLECTION_END && page->items[0].kind == COLLECTION_UNAVAILABLE);
    collection_test_release(page);
    CHECK(collection_reset(collection_test_service, 31) == COLLECTION_OK);
}

static void malformed_and_limits(void)
{
    const CollectionPage *page;
    const char *bad[] = {"{", "[]", "null", "{\"statusCode\":200,\"header\":{\"totalCount\":1},\"entity\":null}",
        "{\"statusCode\":200,\"header\":{\"totalCount\":-1},\"entity\":[]}",
        "{\"statusCode\":200,\"header\":{\"totalCount\":4294967296},\"entity\":[]}", "{\"x\":\"\xff\"}"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        CollectionRequest r = collection_test_fetch(COLLECTION_LIBRARY, 0, 40);
        CHECK(collection_test_deliver(r, bad[i], &page) == COLLECTION_INVALID && page == NULL);
        CHECK(collection_resident_pages(collection_test_service) == 0);
    }
    CollectionRequest r = collection_test_fetch(COLLECTION_LIBRARY, 0, 40);
    CHECK(collection_complete(collection_test_service, r.ticket, COLLECTION_OK, 200,
        r.capacity + 1, &page) == COLLECTION_TOO_LARGE);
    r = collection_test_fetch(COLLECTION_LIBRARY, 0, 40);
    CHECK(collection_complete(collection_test_service, r.ticket, COLLECTION_PARTIAL, 200, 0, &page) == COLLECTION_INVALID);
    CHECK(page == NULL && collection_resident_pages(collection_test_service) == 0);
    r = collection_test_fetch(COLLECTION_LIBRARY, 0, 40);
    CHECK(collection_complete(collection_test_service, r.ticket, COLLECTION_OK, 200,
        SIZE_MAX, &page) == COLLECTION_TOO_LARGE);
    r = collection_test_fetch(COLLECTION_LIBRARY, 0, 40);
    r.body[0] = '[';
    for (size_t i = 1; i < 60000; i += 2) { r.body[i] = '0'; r.body[i+1] = ','; }
    r.body[60000] = ']';
    CHECK(collection_complete(collection_test_service, r.ticket, COLLECTION_OK, 200, 60001, &page) == COLLECTION_NO_MEMORY);
    CHECK(collection_resident_pages(collection_test_service) == 0);
    r = collection_test_fetch(COLLECTION_LIBRARY, 0, 40);
    size_t bytes = (size_t)sprintf(r.body, "{\"statusCode\":200,\"header\":{\"totalCount\":200},\"entity\":[");
    for (unsigned i = 0; i < 129; ++i) { memcpy(r.body + bytes, i ? ",{}" : "{}", i ? 3 : 2); bytes += i ? 3 : 2; }
    memcpy(r.body + bytes, "]}", 2); bytes += 2;
    CHECK(collection_complete(collection_test_service, r.ticket, COLLECTION_OK, 200, bytes, &page) == COLLECTION_TOO_LARGE);
}

static void statuses_and_partial(void)
{
    const CollectionPage *page;
    unsigned statuses[] = {401, 429, 503};
    CollectionResult expected[] = {COLLECTION_AUTH, COLLECTION_RATE_LIMIT, COLLECTION_ERROR};
    for (size_t i = 0; i < 3; ++i) {
        CollectionRequest r = collection_test_fetch(COLLECTION_HOME, 0, 50);
        CHECK(collection_complete(collection_test_service, r.ticket, COLLECTION_OK, statuses[i], 0, &page) == expected[i]);
        CHECK(page == NULL);
    }
    CollectionRequest r = collection_test_fetch(COLLECTION_HOME_SECTION, 0, 51);
    CHECK(collection_test_deliver(r, "{\"errors\":[{\"message\":\"fault injection\"}],\"data\":{\"homeSections\":{"
        "\"__typename\":\"HomeSectionCollection\",\"sections\":[{\"__typename\":\"HomeSection\",\"sectionItems\":{"
        "\"items\":[null,{\"content\":{\"data\":{\"__typename\":\"Future\",\"uri\":\"future:1\"}}}],"
        "\"pagingInfo\":{\"nextOffset\":null}}}]}}}", &page) == COLLECTION_PARTIAL);
    CHECK(page->partial && page->count == 2 && page->items[0].kind == COLLECTION_UNAVAILABLE);
    CHECK(page->items[1].kind == COLLECTION_UNKNOWN && !page->items[1].available);
    collection_test_release(page);
    r = collection_test_fetch(COLLECTION_HOME_SECTION, 0, 52);
    CHECK(collection_test_deliver(r, "{\"errors\":[{}],\"data\":null}", &page) == COLLECTION_ERROR);
    CHECK(collection_reset(collection_test_service, 53) == COLLECTION_OK);
    CHECK(collection_resident_pages(collection_test_service) == 0);
}

void collection_error_tests(void)
{
    CollectionService *invalid = collection_test_service;
    CHECK(collection_service_init(NULL, 0, NULL, &invalid) == COLLECTION_INVALID && invalid == NULL);
    CHECK(collection_configure(NULL, 2, (JsonString){0}) == COLLECTION_INVALID);
    CHECK(collection_configure(collection_test_service, 0, (JsonString){0}) == COLLECTION_INVALID);
    CHECK(collection_configure(collection_test_service, 65, (JsonString){0}) == COLLECTION_INVALID);
    CHECK(collection_configure(collection_test_service, 2, (JsonString){NULL, 1}) == COLLECTION_INVALID);
    CHECK(!page_retain(NULL) && !page_release(NULL));
    generations(); revisions_and_order(); malformed_and_limits(); statuses_and_partial();
}
