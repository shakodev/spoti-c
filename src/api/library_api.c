#include "collection_internal.h"

CollectionResult collection_library_parse(CollectionPage *page, yyjson_val *root, JsonString liked_uri)
{
    uint32_t status;
    if (json_view_uint(root, "statusCode", &status) != JSON_OK) { return COLLECTION_INVALID; }
    CollectionResult result = collection_status(status);
    if (result != COLLECTION_OK) { return result; }
    yyjson_val *header = CGET(root, "header");
    if (json_view_uint(header, "totalCount", &page->total) != JSON_OK) { return COLLECTION_INVALID; }
    yyjson_val *loading = CGET(header, "isLoading");
    if (loading != NULL && !yyjson_is_bool(loading)) { return COLLECTION_INVALID; }
    page->loading = yyjson_get_bool(loading);
    yyjson_val *rows = CGET(root, "entity");
    result = collection_rows(page, rows);
    if (result != COLLECTION_OK) { return result; }
    page->pinned = CGET(root, "pinnedEntity");
    if (page->pinned != NULL && !yyjson_is_arr(page->pinned)) { return COLLECTION_INVALID; }
    if (yyjson_arr_size(page->pinned) > COLLECTION_ROW_CAP) { return COLLECTION_TOO_LARGE; }
    page->pinned_count = (uint32_t)yyjson_arr_size(page->pinned);
    for (uint32_t i = 0; i < page->count; ++i) {
        CollectionItem *item = &page->items[i]; yyjson_val *row = yyjson_arr_get(rows, i);
        item->source = row;
        yyjson_val *info = CGET(row, "entityInfo");
        item->id = item->uri = collection_string(info, "uri");
        item->name = collection_string(info, "name");
        item->pinned = yyjson_get_bool(CGET(info, "pinned"));
        if (yyjson_is_obj(CGET(row, "playlist"))) { item->kind = COLLECTION_PLAYLIST_ITEM; }
        else if (yyjson_is_obj(CGET(row, "folder"))) { item->kind = COLLECTION_FOLDER; }
        else if (yyjson_is_obj(CGET(row, "album"))) { item->kind = COLLECTION_ALBUM; }
        else if (yyjson_is_obj(CGET(row, "artist"))) { item->kind = COLLECTION_ARTIST; }
        else if (yyjson_is_null(row)) { item->kind = COLLECTION_UNAVAILABLE; }
        if (item->kind == COLLECTION_PLAYLIST_ITEM && liked_uri.bytes != 0 &&
            json_span_equal(item->uri, liked_uri)) { item->kind = COLLECTION_LIKED; }
        item->available = item->kind != COLLECTION_UNKNOWN && item->kind != COLLECTION_UNAVAILABLE && item->id.bytes != 0;
        yyjson_val *view = CGET(CGET(row, "playlist"), "canView");
        if (yyjson_is_bool(view) && !yyjson_get_bool(view)) { item->available = false; }
    }
    if (page->count > UINT32_MAX - page->offset) { return COLLECTION_INVALID; }
    uint32_t end = page->offset + page->count;
    if (page->count != 0 && end < page->total) { page->next = end; }
    else if (page->count == 0 && page->offset < page->total) { page->boundary = true; }
    return COLLECTION_OK;
}
