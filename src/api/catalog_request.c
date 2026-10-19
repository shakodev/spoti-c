#include "catalog_internal.h"
#include "protocol/media_source.h"
#include <string.h>

static bool valid_uri(JsonString uri, const char *prefix)
{
    size_t bytes = strlen(prefix); char id[23]; uint8_t gid[16];
    if (uri.data == NULL || uri.bytes != bytes + 22 || memcmp(uri.data, prefix, bytes) != 0) { return false; }
    memcpy(id, uri.data + bytes, 22); id[22] = 0;
    return media_gid_from_base62(id, gid) == MEDIA_OK;
}

CatalogResult search_request(CatalogService *s, JsonString query, SearchCategory category,
    uint32_t offset, uint32_t limit, uint64_t generation, uint64_t now_ms, CatalogRequest *output)
{
    if (output != NULL) { *output = (CatalogRequest){0}; }
    if ((unsigned)category > SEARCH_LIST) { return CATALOG_INVALID; }
    ApiOperation operation = category == SEARCH_TRACKS ? API_SEARCH_TRACKS :
        category == SEARCH_LIST ? API_SEARCH_LIST : API_SEARCH;
    return catalog_prepare(s, operation, CATALOG_SEARCH_FAMILY, query, (uint32_t)category,
        offset, limit, generation, now_ms, output);
}

CatalogResult album_request(CatalogService *s, JsonString uri, uint32_t cursor,
    uint64_t generation, CatalogRequest *output)
{
    if (output != NULL) { *output = (CatalogRequest){0}; }
    if (s == NULL || !valid_uri(uri, "spotify:album:")) { return CATALOG_INVALID; }
    return catalog_prepare(s, cursor == 0 ? API_ALBUM : API_ALBUM_TRACKS, CATALOG_ALBUM_FAMILY,
        uri, 0, cursor, s->config.album_limit, generation, 0, output);
}

CatalogResult artist_request(CatalogService *s, JsonString uri, ArtistSection section,
    uint32_t cursor, uint64_t generation, CatalogRequest *output)
{
    if (output != NULL) { *output = (CatalogRequest){0}; }
    if (s == NULL || !valid_uri(uri, "spotify:artist:")) { return CATALOG_INVALID; }
    if ((unsigned)section > ARTIST_RELATED || cursor != 0) { return CATALOG_UNSUPPORTED; }
    return catalog_prepare(s, API_ARTIST, CATALOG_ARTIST_FAMILY, uri, (uint32_t)section,
        0, CATALOG_PAGE_LIMIT, generation, 0, output);
}

static yyjson_mut_val *variable(CatalogService *s, CatalogSlot *slot, yyjson_mut_doc *doc, const ApiField *field)
{
    const char *name = field->name;
    if (strcmp(name, "searchTerm") == 0 || strcmp(name, "query") == 0 || strcmp(name, "uri") == 0) {
        return yyjson_mut_strncpy(doc, slot->key, slot->page.requested_key.bytes);
    }
    if (strcmp(name, "locale") == 0) { return yyjson_mut_strncpy(doc, s->locale, s->config.locale.bytes); }
    if (strcmp(name, "offset") == 0) { return yyjson_mut_uint(doc, slot->page.offset); }
    if (strcmp(name, "limit") == 0) { return yyjson_mut_uint(doc, slot->page.limit); }
    if (strcmp(name, "numberOfTopResults") == 0) {
        return yyjson_mut_uint(doc, slot->page.operation == API_SEARCH_TRACKS ? 20 : slot->page.limit);
    }
    if (strcmp(name, "isPrefix") == 0) { return yyjson_mut_null(doc); }
    if (strcmp(name, "sectionFilters") == 0) { return yyjson_mut_arr(doc); }
    if (strcmp(name, "preReleaseV2") == 0) { return yyjson_mut_bool(doc, s->config.pre_release_v2); }
    if (strcmp(name, "includePreReleases") == 0) { return yyjson_mut_bool(doc, slot->page.operation != API_SEARCH_TRACKS); }
    if (strcmp(name, "includeAudiobooks") == 0 || strcmp(name, "includeAuthors") == 0 ||
        strcmp(name, "includeEpisodeContentRatingsV2") == 0) { return yyjson_mut_bool(doc, true); }
    if (strcmp(name, "includeAlbumPreReleases") == 0 || strcmp(name, "includeArtistHasConcertsField") == 0) {
        return yyjson_mut_bool(doc, false);
    }
    return NULL;
}

CatalogResult catalog_serialize(CatalogService *s, CatalogSlot *slot)
{
    const ApiContract *contract;
    if (contract_lookup(slot->page.operation, &contract) != API_CONTRACT_OK ||
        contract->transport != API_PATHFINDER) { return CATALOG_UNSUPPORTED; }
    uint64_t scratch[8192]; yyjson_alc allocator;
    if (!yyjson_alc_pool_init(&allocator, scratch, sizeof(scratch))) { return CATALOG_NO_MEMORY; }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(&allocator);
    if (doc == NULL) { return CATALOG_NO_MEMORY; }
    yyjson_mut_val *root = yyjson_mut_obj(doc), *vars = yyjson_mut_obj(doc);
    yyjson_mut_val *extensions = yyjson_mut_obj(doc), *persisted = yyjson_mut_obj(doc);
    bool ok = root != NULL && vars != NULL && extensions != NULL && persisted != NULL;
    yyjson_mut_doc_set_root(doc, root);
    for (size_t i = 0; ok && i < contract->variable_count; ++i) {
        const ApiField *field = &contract->variables[i];
        if (!field->required) { continue; }
        yyjson_mut_val *v = variable(s, slot, doc, field);
        ok = v != NULL && yyjson_mut_obj_add_val(doc, vars, field->name, v);
    }
    ok = ok && yyjson_mut_obj_add_val(doc, root, "variables", vars) &&
        yyjson_mut_obj_add_str(doc, root, "operationName", contract->wire_operation) &&
        yyjson_mut_obj_add_uint(doc, persisted, "version", 1) &&
        yyjson_mut_obj_add_str(doc, persisted, "sha256Hash", contract->persisted_sha256) &&
        yyjson_mut_obj_add_val(doc, extensions, "persistedQuery", persisted) &&
        yyjson_mut_obj_add_val(doc, root, "extensions", extensions);
    size_t bytes = 0; yyjson_write_err error = {0};
    char *json = ok ? yyjson_mut_write_opts(doc, 0, &allocator, &bytes, &error) : NULL;
    CatalogResult result = json == NULL ? (error.code == YYJSON_WRITE_ERROR_INVALID_STRING ?
        CATALOG_INVALID : CATALOG_NO_MEMORY) : bytes > sizeof(slot->upload) ||
        bytes > contract->request_cap ? CATALOG_TOO_LARGE : CATALOG_OK;
    if (result == CATALOG_OK) { memcpy(slot->upload, json, bytes); slot->upload_bytes = bytes; }
    yyjson_mut_doc_free(doc); return result;
}
