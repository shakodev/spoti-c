#include "catalog_internal.h"

CatalogResult catalog_parse_album(CatalogPage *page, uint32_t width, yyjson_val *root)
{
    yyjson_val *album = KGET(KGET(root, "data"), "albumUnion");
    CatalogResult result = catalog_union(album, "Album");
    if (result != CATALOG_OK) { return result; }
    result = catalog_read_item(album, CATALOG_ALBUM, width, &page->detail);
    if (result != CATALOG_OK) { return result; }
    if (page->detail.uri.bytes == 0) {
        page->detail.uri = page->requested_key;
        page->detail.id = catalog_compact_id(page->requested_key, CATALOG_ALBUM);
    }
    yyjson_val *tracks = KGET(album, "tracksV2");
    result = catalog_read_rows(page, KGET(tracks, "items"), width, true);
    if (result != CATALOG_OK) { return result; }
    result = catalog_paging(page, tracks, true);
    if (result != CATALOG_OK) { return result; }
    page->detail.as.album.total_tracks = page->total;
    return CATALOG_OK;
}
