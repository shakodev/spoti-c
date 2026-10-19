#include "catalog_internal.h"

CatalogResult catalog_parse_search(CatalogPage *page, uint32_t selector, uint32_t width, yyjson_val *root)
{
    yyjson_val *search = KGET(KGET(root, "data"), "searchV2");
    if (search == NULL || yyjson_is_null(search)) { return page->partial ? CATALOG_ERROR : CATALOG_NOT_FOUND; }
    if (!yyjson_is_obj(search)) { return CATALOG_INVALID; }
    if (KGET(search, "__typename") != NULL && !catalog_type(search, "SearchResultV2")) { return CATALOG_UNSUPPORTED; }
    const char *field = selector == SEARCH_TRACKS ? "tracksV2" : selector == SEARCH_ALBUMS ? "albumsV2" :
        selector == SEARCH_ARTISTS ? "artists" : "topResultsV2";
    yyjson_val *container = KGET(search, field);
    bool top = selector == SEARCH_TOP || selector == SEARCH_LIST;
    CatalogResult result = catalog_read_rows(page, KGET(container, top ? "itemsV2" : "items"), width, false);
    if (result != CATALOG_OK) { return result; }
    return catalog_paging(page, container, !top);
}
