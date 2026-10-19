#include "catalog_internal.h"

static CatalogResult release_groups(CatalogPage *page, yyjson_val *groups, uint32_t width)
{
    if (!yyjson_is_arr(groups)) { return CATALOG_INVALID; }
    if (yyjson_arr_size(groups) > CATALOG_PAGE_LIMIT) { return CATALOG_TOO_LARGE; }
    size_t i, count; yyjson_val *group;
    yyjson_arr_foreach(groups, i, count, group) {
        yyjson_val *rows = KGET(KGET(group, "releases"), "items");
        if (!yyjson_is_arr(rows)) { return CATALOG_INVALID; }
        size_t j, total; yyjson_val *album;
        yyjson_arr_foreach(rows, j, total, album) {
            if (page->count == CATALOG_PAGE_LIMIT) { return CATALOG_TOO_LARGE; }
            CatalogResult r = catalog_read_item(album, CATALOG_ALBUM, width, &page->items[page->count++]);
            if (r != CATALOG_OK) { return r; }
        }
    }
    return CATALOG_OK;
}

CatalogResult catalog_parse_artist(CatalogPage *page, uint32_t selector, uint32_t width, yyjson_val *root)
{
    yyjson_val *artist = KGET(KGET(root, "data"), "artistUnion");
    CatalogResult result = catalog_union(artist, "Artist");
    if (result != CATALOG_OK) { return result; }
    result = catalog_read_item(artist, CATALOG_ARTIST, width, &page->detail);
    if (result != CATALOG_OK || selector == ARTIST_OVERVIEW) { return result; }
    yyjson_val *discography = KGET(artist, "discography"), *container;
    if (selector == ARTIST_RELATED) { container = KGET(KGET(artist, "relatedContent"), "relatedArtists"); }
    else {
        const char *field = selector == ARTIST_TOP_TRACKS ? "topTracks" : selector == ARTIST_ALBUMS ? "albums" :
            selector == ARTIST_SINGLES ? "singles" : "popularReleasesAlbums";
        container = KGET(discography, field);
    }
    yyjson_val *rows = KGET(container, "items");
    if (!yyjson_is_arr(rows)) { return CATALOG_INVALID; }
    JsonResult total = json_view_uint(container, "totalCount", &page->total);
    if (total == JSON_OK) { page->total_known = true; page->truncated = yyjson_arr_size(rows) < page->total; }
    else if (total != JSON_MISSING) { return CATALOG_INVALID; }
    if (selector == ARTIST_ALBUMS || selector == ARTIST_SINGLES) { return release_groups(page, rows, width); }
    if (selector == ARTIST_TOP_TRACKS) { return catalog_read_rows(page, rows, width, true); }
    if (yyjson_arr_size(rows) > CATALOG_PAGE_LIMIT) { return CATALOG_TOO_LARGE; }
    page->count = (uint32_t)yyjson_arr_size(rows);
    for (uint32_t i = 0; i < page->count; ++i) {
        result = catalog_read_item(yyjson_arr_get(rows, i), selector == ARTIST_RELATED ? CATALOG_ARTIST : CATALOG_ALBUM,
            width, &page->items[i]);
        if (result != CATALOG_OK) { return result; }
    }
    return CATALOG_OK;
}
