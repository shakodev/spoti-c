#include "catalog_test.h"

static void debounce_and_cancel(void)
{
    CatalogRequest old, current, none; const CatalogPage *page;
    CHECK(search_request(catalog_test_service, span("old"), SEARCH_TRACKS, 0, 2, 50, 100, &old) == CATALOG_DEBOUNCING);
    CHECK(old.body == NULL && json_page_pool_resident(catalog_test_pool) == 0);
    CHECK(catalog_next_wait_ms(catalog_test_service, 110) == 15);
    CHECK(search_request(catalog_test_service, span("new"), SEARCH_TRACKS, 0, 2, 51, 110, &current) == CATALOG_DEBOUNCING);
    CHECK(search_request(catalog_test_service, span("old"), SEARCH_TRACKS, 0, 2, 50, 200, &none) == CATALOG_STALE);
    CHECK(catalog_next_wait_ms(catalog_test_service, 135) == 0);
    CHECK(search_request(catalog_test_service, span("new"), SEARCH_TRACKS, 0, 2, 51, 135, &old) == CATALOG_PENDING && old.body != NULL);
    CHECK(search_request(catalog_test_service, span("new"), SEARCH_TRACKS, 0, 2, 51, 135, &none) == CATALOG_PENDING && none.body == NULL);
    CHECK(search_request(catalog_test_service, span("latest"), SEARCH_TRACKS, 0, 2, 52, 140, &none) == CATALOG_DEBOUNCING);
    CHECK(search_request(catalog_test_service, span("latest"), SEARCH_TRACKS, 0, 2, 52, 165, &current) == CATALOG_PENDING && current.body != NULL);
    CHECK(catalog_complete(catalog_test_service, old.ticket, CATALOG_OK, 200, 0, span("bad"), &page) == CATALOG_STALE);
    CHECK(page == NULL && json_page_pool_resident(catalog_test_pool) == 1);
    CHECK(catalog_cancel(catalog_test_service, current.ticket) == CATALOG_PENDING);
    CHECK(json_page_pool_resident(catalog_test_pool) == 1);
    CHECK(catalog_complete(catalog_test_service, current.ticket, CATALOG_OK, 200, 0, span("bad"), &page) == CATALOG_CANCELLED);
    CHECK(catalog_cancel(catalog_test_service, old.ticket) == CATALOG_STALE);
    CHECK(search_request(catalog_test_service, span("cancel debounce"), SEARCH_TOP, 0, 2, 53, 170, &none) == CATALOG_DEBOUNCING);
    CHECK(catalog_cancel_generation(catalog_test_service, 53) == CATALOG_OK);
    CHECK(catalog_next_wait_ms(catalog_test_service, 200) == UINT32_MAX);
    CHECK(search_request(catalog_test_service, span("cancel debounce"), SEARCH_TOP, 0, 2, 53, 200, &none) == CATALOG_CANCELLED);
}

static const char mixed[] = "{\"errors\":[{\"message\":\"local fault injection\"}],\"data\":{\"searchV2\":{\"tracksV2\":{\"totalCount\":4,"
    "\"pagingInfo\":{\"nextOffset\":null},\"items\":[{\"item\":{\"__typename\":\"TrackResponseWrapper\",\"data\":{\"__typename\":\"Track\","
    "\"uri\":\"spotify:track:0000000000000000000001\",\"name\":\"Bj\\u00f6rk \\ud83c\\udfb5\",\"duration\":{\"totalMilliseconds\":123},"
    "\"playability\":{\"playable\":true},\"albumOfTrack\":{\"coverArt\":{\"sources\":[{\"url\":\"large\",\"width\":640},"
    "{\"url\":\"small\",\"width\":64},{\"url\":\"chosen\",\"width\":300}]}}}}},null,"
    "{\"item\":{\"__typename\":\"TrackResponseWrapper\",\"data\":{\"__typename\":\"Album\",\"name\":\"wrong union\"}}},"
    "{\"item\":{\"__typename\":\"FutureResponseWrapper\",\"data\":{\"__typename\":\"Track\"}}}]}}}}";

static void typed_and_unicode(void)
{
    const CatalogPage *page; const char *query = "Bj\xc3\xb6rk \"\\\n\xf0\x9f\x8e\xb5";
    CatalogRequest r = catalog_test_search(query, SEARCH_TRACKS, 0, 4, 60);
    yyjson_doc *doc = yyjson_read(r.upload, r.upload_bytes, 0); CHECK(doc != NULL);
    yyjson_val *term = yyjson_ptr_get(yyjson_doc_get_root(doc), "/variables/searchTerm");
    CHECK(yyjson_get_len(term) == strlen(query) && memcmp(yyjson_get_str(term), query, strlen(query)) == 0);
    yyjson_doc_free(doc);
    CHECK(catalog_test_deliver(r, mixed, &page) == CATALOG_PARTIAL);
    CHECK(page->count == 4 && page->next == CATALOG_END && page->items[0].kind == CATALOG_TRACK);
    CHECK(page->items[0].id.valid && page->items[0].id.bytes[15] == 1);
    CHECK(page->items[0].as.track.playable_id.valid && page->items[0].as.track.duration_ms == 123);
    CHECK(page->items[0].name.bytes == 11 && json_span_equal(page->items[0].image.url, span("chosen")));
    CHECK(page->items[1].kind == CATALOG_ITEM_UNAVAILABLE);
    CHECK(page->items[2].kind == CATALOG_ITEM_UNSUPPORTED && page->items[3].kind == CATALOG_ITEM_UNSUPPORTED);
    catalog_test_release(page);
    r = catalog_test_search("", SEARCH_TRACKS, 0, 2, 61);
    CHECK(catalog_test_deliver(r, "{\"data\":{\"searchV2\":{\"tracksV2\":{\"totalCount\":0,\"items\":[]}}}}", &page) == CATALOG_OK);
    CHECK(page->count == 0 && page->next == CATALOG_END); catalog_test_release(page);
    r = catalog_test_search("restricted", SEARCH_TRACKS, 0, 2, 62);
    CHECK(catalog_test_deliver(r, "{\"data\":{\"searchV2\":{\"tracksV2\":{\"totalCount\":1,\"items\":[{\"item\":{\"__typename\":\"TrackResponseWrapper\","
        "\"data\":{\"__typename\":\"Track\",\"uri\":\"spotify:track:0000000000000000000001\",\"playability\":{\"playable\":false,\"reason\":\"market\"}}}}]}}}}", &page) == CATALOG_OK);
    CHECK(page->items[0].id.valid && !page->items[0].available && !page->items[0].as.track.playable_id.valid);
    CHECK(page->items[0].as.track.playable_uri.bytes == 0); catalog_test_release(page);
    CHECK(catalog_reset(catalog_test_service, 63) == CATALOG_OK);
}

static void failures(void)
{
    const CatalogPage *page; CatalogRequest r;
    const char *bodies[] = {"{", "[]", "{\"errors\":{}}", "{\"data\":{\"albumUnion\":null}}",
        "{\"data\":{\"albumUnion\":{\"__typename\":\"RestrictedContent\"}}}",
        "{\"data\":{\"albumUnion\":{\"__typename\":\"FutureAlbum\"}}}"};
    const CatalogResult results[] = {CATALOG_INVALID,CATALOG_INVALID,CATALOG_INVALID,CATALOG_NOT_FOUND,CATALOG_RESTRICTED,CATALOG_UNSUPPORTED};
    for (size_t i = 0; i < sizeof(bodies)/sizeof(bodies[0]); ++i) {
        r = catalog_test_album(0, 70);
        CHECK(catalog_test_deliver(r, bodies[i], &page) == results[i] && page == NULL);
        CHECK(json_page_pool_resident(catalog_test_pool) == 0);
    }
    r = catalog_test_artist(ARTIST_OVERVIEW, 71);
    CHECK(catalog_test_deliver(r, "{\"data\":{\"artistUnion\":null}}", &page) == CATALOG_NOT_FOUND);
    r = catalog_test_album(0, 72);
    CHECK(catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, 200, 0, span("wrong hash"), &page) == CATALOG_HASH_MISMATCH);
    for (unsigned i = 0; i < 3; ++i) {
        r = catalog_test_album(0, 72);
        CHECK(catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, i == 0 ? 401 : i == 1 ? 429 : 503, 0,
            span(r.contract->persisted_sha256), &page) == (i == 0 ? CATALOG_AUTH : i == 1 ? CATALOG_RATE_LIMIT : CATALOG_ERROR));
    }
    r = catalog_test_album(0, 72);
    CHECK(catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, 200, SIZE_MAX, span(r.contract->persisted_sha256), &page) == CATALOG_TOO_LARGE);
    r = catalog_test_album(0, 72);
    r.body[0] = '[';
    for (size_t i = 1; i < 60000; i += 2) { r.body[i] = '0'; r.body[i+1] = ','; }
    r.body[60000] = ']';
    CHECK(catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, 200, 60001,
        span(r.contract->persisted_sha256), &page) == CATALOG_NO_MEMORY);
    CHECK(search_request(catalog_test_service, span("\xff"), SEARCH_TRACKS, 0, 2, 73, 0, &r) == CATALOG_DEBOUNCING);
    CHECK(search_request(catalog_test_service, span("\xff"), SEARCH_TRACKS, 0, 2, 73, 25, &r) == CATALOG_INVALID);
    CHECK(r.body == NULL && json_page_pool_resident(catalog_test_pool) == 0);
}

static void limits_and_order(void)
{
    CatalogRequest r; const CatalogPage *page;
    CHECK(search_request(catalog_test_service, span("x"), SEARCH_TRACKS, 0, 101, 80, 0, &r) == CATALOG_INVALID);
    CHECK(search_request(catalog_test_service, span("x"), SEARCH_TRACKS, 0, 0, 80, 0, &r) == CATALOG_INVALID);
    CHECK(artist_request(catalog_test_service, span(catalog_artist_uri), ARTIST_ALBUMS, 1, 80, &r) == CATALOG_UNSUPPORTED);
    CHECK(album_request(catalog_test_service, span("wrong-uri"), 0, 80, &r) == CATALOG_INVALID);
    CHECK(search_request(catalog_test_service, span("x"), SEARCH_TRACKS, 2, 2, 80, 0, &r) == CATALOG_ORDER);
    r = catalog_test_search("x", SEARCH_TRACKS, 0, 2, 81);
    CHECK(catalog_test_fixture(r, "search_tracks-observed-positive", &page) == CATALOG_OK);
    catalog_test_release(page);
    CHECK(search_request(catalog_test_service, span("different"), SEARCH_TRACKS, 2, 2, 81, 25, &r) == CATALOG_INVALID);
    CHECK(search_request(catalog_test_service, span("x"), SEARCH_TOP, 2, 2, 81, 25, &r) == CATALOG_INVALID);
    CHECK(search_request(catalog_test_service, span("x"), SEARCH_TRACKS, 1, 2, 81, 25, &r) == CATALOG_ORDER);
    r = catalog_test_search("x", SEARCH_TRACKS, 2, 2, 81);
    CHECK(catalog_test_deliver(r, "{\"data\":{\"searchV2\":{\"tracksV2\":{\"totalCount\":9,\"pagingInfo\":{\"nextOffset\":2},\"items\":[null]}}}}", &page) == CATALOG_BOUNDARY);
    CHECK(page->next == CATALOG_END); catalog_test_release(page);
    CHECK(catalog_reset(catalog_test_service, 82) == CATALOG_OK);
}

void catalog_error_tests(void)
{
    debounce_and_cancel(); typed_and_unicode(); failures(); limits_and_order();
}
