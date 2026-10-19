#include "collection_internal.h"

CollectionResult collection_playlist_parse(CollectionPage *page, yyjson_val *root, uint32_t limit)
{
    uint32_t status, start, length;
    if (json_view_uint(CGET(root, "status"), "statusCode", &status) != JSON_OK) { return COLLECTION_INVALID; }
    CollectionResult result = collection_status(status);
    if (result != COLLECTION_OK) { return result; }
    yyjson_val *range = CGET(CGET(root, "query"), "range");
    if (json_view_uint(range, "start", &start) != JSON_OK ||
        json_view_uint(range, "length", &length) != JSON_OK) { return COLLECTION_INVALID; }
    if (start != page->offset || length != limit) { return COLLECTION_ORDER; }
    yyjson_val *data = CGET(root, "data"), *rows = CGET(data, "item");
    result = collection_rows(page, rows);
    if (result != COLLECTION_OK) { return result; }
    if (page->count > length || page->count > UINT32_MAX - page->offset) { return COLLECTION_INVALID; }
    if (json_view_uint(data, "unrangedLength", &page->total) != JSON_OK) { return COLLECTION_INVALID; }
    page->metadata = CGET(CGET(data, "playlist"), "playlistMetadata");
    page->name = collection_string(page->metadata, "name");
    page->revision = collection_string(data, "contentSignature");
    page->loading = yyjson_get_bool(CGET(data, "loadingContents"));
    for (uint32_t i = 0; i < page->count; ++i) {
        CollectionItem *item = &page->items[i]; yyjson_val *row = yyjson_arr_get(rows, i);
        item->source = row; item->uri = collection_string(row, "uri");
        item->id = collection_string(row, "rowId");
        yyjson_val *track = CGET(row, "trackMetadata");
        if (item->uri.bytes == 0) { item->uri = collection_string(track, "link"); }
        item->name = collection_string(track, "name");
        item->kind = yyjson_is_obj(track) ? COLLECTION_TRACK : COLLECTION_UNKNOWN;
        item->available = yyjson_get_bool(CGET(CGET(row, "trackPlayState"), "isPlayable"));
        if (yyjson_is_null(row) || item->uri.bytes == 0) { item->kind = COLLECTION_UNAVAILABLE; item->available = false; }
    }
    uint32_t end = page->offset + page->count;
    /* Zero is also a generated-client default: nonempty undecorated rows do
     * not prove exhaustion. Ask the adapter for the next bounded range. */
    if (page->count != 0 && (page->total == 0 || end < page->total)) { page->next = end; }
    else if (page->count == 0 && page->offset < page->total) { page->boundary = true; }
    return COLLECTION_OK;
}
