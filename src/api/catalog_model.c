#include "catalog_internal.h"
#include "protocol/media_source.h"
#include <string.h>

JsonString catalog_string(yyjson_val *object, const char *key)
{
    JsonString value = {0}; (void)json_view_string(object, key, &value); return value;
}
bool catalog_type(yyjson_val *object, const char *type)
{
    return json_span_equal(catalog_string(object, "__typename"), (JsonString){type, strlen(type)});
}
CatalogResult catalog_union(yyjson_val *value, const char *expected)
{
    if (value == NULL || yyjson_is_null(value)) { return CATALOG_NOT_FOUND; }
    if (!yyjson_is_obj(value)) { return CATALOG_INVALID; }
    if (catalog_type(value, expected)) { return CATALOG_OK; }
    if (catalog_type(value, "NotFound")) { return CATALOG_NOT_FOUND; }
    if (catalog_type(value, "RestrictedContent")) { return CATALOG_RESTRICTED; }
    return catalog_type(value, "GenericError") ? CATALOG_ERROR : CATALOG_UNSUPPORTED;
}

CatalogId catalog_compact_id(JsonString uri, CatalogKind kind)
{
    CatalogId id = {0};
    const char *prefix = kind == CATALOG_TRACK ? "spotify:track:" :
        kind == CATALOG_ALBUM ? "spotify:album:" : "spotify:artist:";
    size_t bytes = strlen(prefix);
    if (uri.data != NULL && uri.bytes == bytes + 22 && memcmp(uri.data, prefix, bytes) == 0) {
        char text[23]; memcpy(text, uri.data + bytes, 22); text[22] = 0;
        id.valid = media_gid_from_base62(text, id.bytes) == MEDIA_OK;
    }
    return id;
}

static CatalogResult image(yyjson_val *sources, uint32_t target, CatalogImage *output)
{
    if (sources == NULL || yyjson_is_null(sources)) { return CATALOG_OK; }
    if (!yyjson_is_arr(sources)) { return CATALOG_INVALID; }
    if (yyjson_arr_size(sources) > 32) { return CATALOG_TOO_LARGE; }
    size_t i, count; yyjson_val *source;
    yyjson_arr_foreach(sources, i, count, source) {
        CatalogImage candidate = {0}; candidate.url = catalog_string(source, "url");
        if (candidate.url.bytes == 0) { continue; }
        JsonResult width = json_view_uint(source, "width", &candidate.width);
        JsonResult height = json_view_uint(source, "height", &candidate.height);
        if ((width != JSON_OK && width != JSON_NULL && width != JSON_MISSING) ||
            (height != JSON_OK && height != JSON_NULL && height != JSON_MISSING)) { return CATALOG_INVALID; }
        bool choose = output->url.bytes == 0;
        if (candidate.width != 0 && (output->width == 0 ||
            (output->width < target && candidate.width > output->width) ||
            (candidate.width >= target && output->width >= target && candidate.width < output->width))) { choose = true; }
        if (choose) { *output = candidate; }
    }
    return CATALOG_OK;
}

CatalogResult catalog_read_item(yyjson_val *data, CatalogKind kind, uint32_t width, CatalogItem *item)
{
    *item = (CatalogItem){0};
    if (data == NULL || yyjson_is_null(data)) { item->kind = CATALOG_ITEM_UNAVAILABLE; return CATALOG_OK; }
    if (!yyjson_is_obj(data)) { return CATALOG_INVALID; }
    item->kind = kind; item->discriminator = catalog_string(data, "__typename");
    const char *expected = kind == CATALOG_TRACK ? "Track" : kind == CATALOG_ALBUM ? "Album" : "Artist";
    if (item->discriminator.bytes != 0 && !catalog_type(data, expected)) {
        item->kind = catalog_type(data, "NotFound") || catalog_type(data, "RestrictedContent") ?
            CATALOG_ITEM_UNAVAILABLE : CATALOG_ITEM_UNSUPPORTED; return CATALOG_OK;
    }
    item->uri = catalog_string(data, "uri");
    item->name = catalog_string(kind == CATALOG_ARTIST ? KGET(data, "profile") : data, "name");
    item->id = catalog_compact_id(item->uri, kind);
    yyjson_val *availability = KGET(KGET(data, "playability"), "playable");
    if (availability != NULL && !yyjson_is_bool(availability)) { return CATALOG_INVALID; }
    item->available = kind == CATALOG_ARTIST || yyjson_get_bool(availability);
    item->restriction = catalog_string(KGET(data, "playability"), "reason");
    yyjson_val *cover = KGET(data, "coverArt");
    if (kind == CATALOG_ARTIST) { cover = KGET(KGET(data, "visuals"), "avatarImage"); }
    if (kind == CATALOG_TRACK) {
        yyjson_val *album = KGET(data, "albumOfTrack"); cover = KGET(album, "coverArt");
        item->as.track.album_uri = catalog_string(album, "uri");
        item->as.track.artist_name = catalog_string(KGET(yyjson_arr_get(KGET(KGET(data, "artists"), "items"), 0), "profile"), "name");
        JsonResult duration = json_view_uint(KGET(data, "duration"), "totalMilliseconds", &item->as.track.duration_ms);
        if (duration != JSON_OK && duration != JSON_MISSING && duration != JSON_NULL) { return CATALOG_INVALID; }
        item->as.track.relinking = KGET(data, "relinkingInformation");
        if (item->available && (item->as.track.relinking == NULL || yyjson_is_null(item->as.track.relinking))) {
            item->as.track.playable_uri = item->uri; item->as.track.playable_id = item->id;
        }
        /* Non-null relinking has no observed field schema in the pinned fixture
         * set. Preserve it for inspection; never manufacture a playable ID. */
    } else if (kind == CATALOG_ALBUM) {
        item->as.album.artist_name = catalog_string(KGET(yyjson_arr_get(KGET(KGET(data, "artists"), "items"), 0), "profile"), "name");
        JsonResult total = json_view_uint(KGET(data, "tracks"), "totalCount", &item->as.album.total_tracks);
        if (total != JSON_OK && total != JSON_MISSING) { return CATALOG_INVALID; }
    } else {
        JsonResult followers = json_base_u64(KGET(KGET(data, "stats"), "followers"), UINT64_MAX, &item->as.artist.followers);
        if (followers != JSON_OK && followers != JSON_MISSING) { return CATALOG_INVALID; }
    }
    return image(KGET(cover, "sources"), width, &item->image);
}

CatalogResult catalog_read_wrapper(yyjson_val *wrapper, uint32_t width, CatalogItem *item)
{
    *item = (CatalogItem){0};
    if (wrapper == NULL || yyjson_is_null(wrapper)) { item->kind = CATALOG_ITEM_UNAVAILABLE; return CATALOG_OK; }
    if (!yyjson_is_obj(wrapper)) { return CATALOG_INVALID; }
    CatalogKind kind;
    if (catalog_type(wrapper, "TrackResponseWrapper")) { kind = CATALOG_TRACK; }
    else if (catalog_type(wrapper, "AlbumResponseWrapper")) { kind = CATALOG_ALBUM; }
    else if (catalog_type(wrapper, "ArtistResponseWrapper")) { kind = CATALOG_ARTIST; }
    else { item->discriminator = catalog_string(wrapper, "__typename"); return CATALOG_OK; }
    yyjson_val *data = KGET(wrapper, "data");
    const char *type = kind == CATALOG_TRACK ? "Track" : kind == CATALOG_ALBUM ? "Album" : "Artist";
    if (yyjson_is_obj(data) && !catalog_type(data, type) && !catalog_type(data, "NotFound") &&
        !catalog_type(data, "RestrictedContent")) {
        item->discriminator = catalog_string(data, "__typename"); return CATALOG_OK;
    }
    return catalog_read_item(data, kind, width, item);
}

CatalogResult catalog_read_rows(CatalogPage *page, yyjson_val *rows, uint32_t width, bool tracks)
{
    if (!yyjson_is_arr(rows)) { return CATALOG_INVALID; }
    if (yyjson_arr_size(rows) > CATALOG_PAGE_LIMIT || yyjson_arr_size(rows) > page->limit) { return CATALOG_TOO_LARGE; }
    page->count = (uint32_t)yyjson_arr_size(rows);
    for (uint32_t i = 0; i < page->count; ++i) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        CatalogResult result = tracks ? catalog_read_item(KGET(row, "track"), CATALOG_TRACK, width, &page->items[i]) :
            catalog_read_wrapper(KGET(row, "item") != NULL ? KGET(row, "item") : row, width, &page->items[i]);
        if (result != CATALOG_OK) { return result; }
    }
    return CATALOG_OK;
}

CatalogResult catalog_paging(CatalogPage *page, yyjson_val *container, bool has_total)
{
    if (page->count > UINT32_MAX - page->offset) { return CATALOG_INVALID; }
    if (has_total) {
        if (json_view_uint(container, "totalCount", &page->total) != JSON_OK) { return CATALOG_INVALID; }
        page->total_known = true;
    }
    yyjson_val *paging = KGET(container, "pagingInfo");
    if (paging != NULL) {
        uint32_t next;
        JsonResult r = json_view_uint(paging, "nextOffset", &next);
        if (r == JSON_NULL) { return CATALOG_OK; }
        if (r != JSON_OK) { return CATALOG_INVALID; }
        if (next <= page->offset || page->count == 0) { page->boundary = true; }
        else { page->next = next; }
    } else if (page->count != 0 && (has_total ? page->offset + page->count < page->total : page->count == page->limit)) {
        page->next = page->offset + page->count;
    }
    return CATALOG_OK;
}
