#include "catalog_test.h"
#include "core/memory_budget.h"
#include <stdalign.h>

static alignas(16) unsigned char pool_storage[MEMORY_BUDGET_API_PAGES];
static alignas(16) unsigned char models[MEMORY_BUDGET_MODEL - COLLECTION_MODEL_BUDGET];
static alignas(16) unsigned char collection_models[COLLECTION_MODEL_BUDGET];
CatalogService *catalog_test_service;
CollectionService *catalog_test_collections;
JsonPagePool *catalog_test_pool;
const char catalog_album_uri[] = "spotify:album:0000000000000000000001";
const char catalog_artist_uri[] = "spotify:artist:0000000000000000000002";

CatalogRequest catalog_test_search(const char *query, SearchCategory category,
    uint32_t offset, uint32_t limit, uint64_t generation)
{
    CatalogRequest r;
    CatalogResult result = search_request(catalog_test_service, span(query), category, offset, limit, generation, 0, &r);
    if (result == CATALOG_DEBOUNCING) {
        CHECK(r.body == NULL);
        result = search_request(catalog_test_service, span(query), category, offset, limit, generation, 25, &r);
    }
    CHECK(result == CATALOG_PENDING && r.body != NULL && r.upload != NULL);
    return r;
}
CatalogRequest catalog_test_album(uint32_t offset, uint64_t generation)
{
    CatalogRequest r;
    CHECK(album_request(catalog_test_service, span(catalog_album_uri), offset, generation, &r) == CATALOG_PENDING);
    CHECK(r.body != NULL); return r;
}
CatalogRequest catalog_test_artist(ArtistSection section, uint64_t generation)
{
    CatalogRequest r;
    CHECK(artist_request(catalog_test_service, span(catalog_artist_uri), section, 0, generation, &r) == CATALOG_PENDING);
    CHECK(r.body != NULL); return r;
}
CatalogResult catalog_test_deliver(CatalogRequest r, const char *json, const CatalogPage **page)
{
    size_t bytes = strlen(json); CHECK(bytes <= r.capacity); memcpy(r.body, json, bytes);
    return catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, 200, bytes,
        span(r.contract->persisted_sha256), page);
}
CatalogResult catalog_test_fixture(CatalogRequest r, const char *name, const CatalogPage **page)
{
    char path[512]; CHECK(snprintf(path, sizeof(path), "%s/%s.json", API_FIXTURE_DIR, name) > 0);
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL); CHECK(doc != NULL);
    yyjson_val *root = yyjson_doc_get_root(doc), *request = yyjson_obj_get(root, "request");
    yyjson_val *envelope = yyjson_obj_get(request, "body");
    CHECK(strcmp(yyjson_get_str(yyjson_obj_get(envelope, "operationName")), r.contract->wire_operation) == 0);
    const char *hash = yyjson_get_str(yyjson_ptr_get(envelope, "/extensions/persistedQuery/sha256Hash"));
    CHECK(hash != NULL && strcmp(hash, r.contract->persisted_sha256) == 0);
    yyjson_val *response = yyjson_obj_get(root, "response"); size_t bytes;
    char *json = yyjson_val_write(yyjson_obj_get(response, "body"), 0, &bytes);
    CHECK(json != NULL && bytes <= r.capacity); memcpy(r.body, json, bytes);
    unsigned status = (unsigned)yyjson_get_uint(yyjson_obj_get(response, "status"));
    size_t values = yyjson_doc_get_val_count(doc);
    free(json); yyjson_doc_free(doc);
    CatalogResult result = catalog_complete(catalog_test_service, r.ticket, CATALOG_OK, status, bytes,
        span(r.contract->persisted_sha256), page);
    if (result != CATALOG_OK && result != CATALOG_NOT_FOUND && result != CATALOG_ERROR) {
        printf("%s: result %u, body %zu bytes, fixture %zu values\n", name, (unsigned)result, bytes, values);
    }
    return result;
}

static void check_envelope(CatalogRequest r, const char *query_key)
{
    yyjson_doc *doc = yyjson_read(r.upload, r.upload_bytes, 0); CHECK(doc != NULL);
    yyjson_val *root = yyjson_doc_get_root(doc), *vars = yyjson_obj_get(root, "variables");
    CHECK(r.contract == contract_inspect(r.contract->operation));
    CHECK(strcmp(yyjson_get_str(yyjson_obj_get(root, "operationName")), r.contract->wire_operation) == 0);
    CHECK(strcmp(yyjson_get_str(yyjson_ptr_get(root, "/extensions/persistedQuery/sha256Hash")), r.contract->persisted_sha256) == 0);
    CHECK(r.contract->transport == API_PATHFINDER && r.contract->method == API_METHOD_POST);
    CHECK(r.contract->header_count == 6);
    for (size_t i = 0; i < r.contract->variable_count; ++i) {
        if (r.contract->variables[i].required) { CHECK(yyjson_obj_get(vars, r.contract->variables[i].name) != NULL); }
    }
    if (query_key != NULL) {
        CHECK(yyjson_is_str(yyjson_obj_get(vars, query_key)));
        CHECK(yyjson_obj_get(vars, strcmp(query_key, "query") == 0 ? "searchTerm" : "query") == NULL);
    }
    yyjson_doc_free(doc);
}

static void search_fixtures(void)
{
    const CatalogPage *page; CatalogRequest r;
    const SearchCategory categories[] = {SEARCH_TOP, SEARCH_TRACKS, SEARCH_LIST};
    const char *names[] = {"search", "search_tracks", "search_list"};
    for (size_t i = 0; i < 3; ++i) {
        uint32_t limit = i == 2 ? 50 : 2; uint64_t gen = 1 + i*3;
        r = catalog_test_search("redacted", categories[i], 0, limit, gen); check_envelope(r, i == 2 ? "query" : "searchTerm");
        char name[80]; CHECK(snprintf(name, sizeof(name), "%s-observed-positive", names[i]) > 0);
        CHECK(catalog_test_fixture(r, name, &page) == CATALOG_OK);
        CHECK(page->count == limit && page->next == limit);
        if (i == 1) { CHECK(page->items[0].kind == CATALOG_TRACK && page->total_known && page->total == 431); }
        if (i == 2) {
            CHECK(page->items[0].kind == CATALOG_ARTIST && !page->total_known);
            CHECK(page->items[1].kind == CATALOG_ITEM_UNSUPPORTED && page->items[2].kind == CATALOG_TRACK);
        }
        printf("%s fixture: %u rows, %zu DOM values\n", names[i], page->count, yyjson_doc_get_val_count(page->json.doc));
        catalog_test_release(page);
        r = catalog_test_search("redacted", categories[i], limit, limit, gen);
        CHECK(snprintf(name, sizeof(name), "%s-observed-pagination", names[i]) > 0);
        CHECK(catalog_test_fixture(r, name, &page) == CATALOG_OK && page->count == 2);
        CHECK(page->next == (i == 2 ? CATALOG_END : 4)); catalog_test_release(page);
        r = catalog_test_search("redacted", categories[i], 0, limit, gen+1);
        CHECK(snprintf(name, sizeof(name), "%s-observed-error", names[i]) > 0);
        CHECK(catalog_test_fixture(r, name, &page) == CATALOG_ERROR && page == NULL);
    }
    r = catalog_test_search("redacted", SEARCH_ALBUMS, 0, 2, 11);
    CHECK(catalog_test_fixture(r, "search-observed-positive", &page) == CATALOG_OK);
    CHECK(page->count == 2 && page->items[0].kind == CATALOG_ALBUM && page->total == 197);
    catalog_test_release(page);
    r = catalog_test_search("redacted", SEARCH_ARTISTS, 0, 2, 12);
    CHECK(catalog_test_fixture(r, "search-observed-positive", &page) == CATALOG_OK);
    CHECK(page->count == 2 && page->items[0].kind == CATALOG_ARTIST && page->total == 116);
    catalog_test_release(page);
}

static void detail_fixtures(void)
{
    const CatalogPage *page; CatalogRequest r = catalog_test_album(0, 20); check_envelope(r, NULL);
    CHECK(catalog_test_fixture(r, "album-observed-positive", &page) == CATALOG_OK);
    CHECK(page->detail.kind == CATALOG_ALBUM && page->count == 2 && page->total == 14 && page->next == 2);
    CHECK(page->items[0].kind == CATALOG_TRACK && page->items[0].as.track.duration_ms == 320357);
    catalog_test_release(page);
    r = catalog_test_album(2, 20);
    CHECK(r.contract->operation == API_ALBUM_TRACKS);
    CHECK(catalog_test_fixture(r, "album_tracks-observed-pagination", &page) == CATALOG_OK);
    CHECK(page->detail.id.valid && page->detail.id.bytes[15] == 1);
    CHECK(page->count == 2 && page->offset == 2 && page->next == 4); catalog_test_release(page);
    r = catalog_test_album(4, 20);
    CHECK(catalog_test_fixture(r, "album_tracks-observed-error", &page) == CATALOG_NOT_FOUND && page == NULL);
    r = catalog_test_album(4, 20);
    CHECK(catalog_test_fixture(r, "album_tracks-observed-positive", &page) == CATALOG_OK);
    catalog_test_release(page);
    r = catalog_test_album(0, 21);
    CHECK(catalog_test_fixture(r, "album-observed-pagination", &page) == CATALOG_OK && page->next == CATALOG_END);
    catalog_test_release(page);
    r = catalog_test_album(0, 22);
    CHECK(catalog_test_fixture(r, "album-observed-error", &page) == CATALOG_NOT_FOUND);
    for (unsigned section = ARTIST_OVERVIEW; section <= ARTIST_RELATED; ++section) {
        r = catalog_test_artist((ArtistSection)section, 30+section); check_envelope(r, NULL);
        CHECK(catalog_test_fixture(r, "artist-observed-positive", &page) == CATALOG_OK);
        CHECK(page->detail.kind == CATALOG_ARTIST && page->detail.as.artist.followers == 11857007);
        CHECK(page->next == CATALOG_END);
        CHECK(page->count == (section == ARTIST_OVERVIEW ? 0u : section == ARTIST_RELATED ? 20u : 10u));
        if (section == ARTIST_ALBUMS) { CHECK(page->truncated && page->total == 15 && page->items[0].kind == CATALOG_ALBUM); }
        catalog_test_release(page);
    }
    r = catalog_test_artist(ARTIST_OVERVIEW, 40);
    CHECK(catalog_test_fixture(r, "artist-observed-error", &page) == CATALOG_NOT_FOUND && page == NULL);
    CHECK(strcmp(contract_header_override(API_ARTIST, "Spotify-App-Version"), "896000000") == 0);
    CHECK(strcmp(contract_header_override(API_ALBUM_TRACKS, "Spotify-App-Version"), "896000000") == 0);
    CHECK(contract_header_override(API_SEARCH, "Spotify-App-Version") == NULL);
    CHECK(contract_header_override(API_ARTIST, "Authorization") == NULL);
    CHECK(catalog_reset(catalog_test_service, 41) == CATALOG_OK && json_page_pool_resident(catalog_test_pool) == 0);
}

int main(void)
{
    CatalogConfig config = {{"", 0}, 25, 192, 2, true};
    CHECK(json_page_pool_size() <= sizeof(pool_storage));
    CHECK(catalog_service_size() + collection_service_size() <= MEMORY_BUDGET_MODEL);
    CHECK(json_page_pool_init(pool_storage, sizeof(pool_storage), &catalog_test_pool) == JSON_OK);
    CHECK(collection_service_init(collection_models, sizeof(collection_models), catalog_test_pool, &catalog_test_collections) == COLLECTION_OK);
    CHECK(catalog_service_init(models, sizeof(models), catalog_test_pool, &config, &catalog_test_service) == CATALOG_OK);
    search_fixtures(); detail_fixtures(); catalog_error_tests(); catalog_pool_tests();
    printf("Catalog passed: shared pool %zu bytes; catalog/collection models %zu/%zu bytes\n",
        json_page_pool_size(), catalog_service_size(), collection_service_size()); return 0;
}
