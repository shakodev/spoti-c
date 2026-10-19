#include "collection_internal.h"
#include <string.h>

static bool type_is(JsonString type, const char *expected)
{
    return json_span_equal(type, (JsonString){expected, strlen(expected)});
}

CollectionItem collection_home_item(yyjson_val *value)
{
    CollectionItem item = {0}; item.source = value;
    if (yyjson_is_null(value) || value == NULL) { item.kind = COLLECTION_UNAVAILABLE; return item; }
    yyjson_val *data = CGET(CGET(value, "content"), "data");
    item.discriminator = collection_string(data, "__typename");
    item.uri = collection_string(data, "uri");
    item.id = collection_string(value, "uri");
    if (item.id.bytes == 0) { item.id = item.uri; }
    item.name = collection_string(data, "name");
    if (type_is(item.discriminator, "Track")) { item.kind = COLLECTION_TRACK; }
    else if (type_is(item.discriminator, "Album")) { item.kind = COLLECTION_ALBUM; }
    else if (type_is(item.discriminator, "Artist")) { item.kind = COLLECTION_ARTIST; }
    else if (type_is(item.discriminator, "Playlist")) { item.kind = COLLECTION_PLAYLIST_ITEM; }
    else if (type_is(item.discriminator, "Episode")) { item.kind = COLLECTION_EPISODE; }
    else if (type_is(item.discriminator, "NotFound") || yyjson_is_null(data)) {
        item.kind = COLLECTION_UNAVAILABLE;
    }
    item.available = item.kind != COLLECTION_UNKNOWN && item.kind != COLLECTION_UNAVAILABLE && item.uri.bytes != 0;
    yyjson_val *playable = CGET(CGET(data, "playability"), "playable");
    if (yyjson_is_bool(playable) && !yyjson_get_bool(playable)) { item.available = false; }
    collection_compact_id(&item);
    return item;
}

CollectionResult collection_home_parse(CollectionPage *page, yyjson_val *root)
{
    yyjson_val *errors = CGET(root, "errors");
    if (errors != NULL && !yyjson_is_arr(errors)) { return COLLECTION_INVALID; }
    page->partial = yyjson_arr_size(errors) != 0;
    yyjson_val *data = CGET(root, "data"), *rows = NULL;
    if (page->kind == COLLECTION_HOME) {
        yyjson_val *home = CGET(data, "home");
        if (!type_is(collection_string(home, "__typename"), "HomeResponsePayload")) { return COLLECTION_ERROR; }
        yyjson_val *sections = CGET(CGET(home, "sectionContainer"), "sections");
        rows = CGET(sections, "items");
        if (json_view_uint(sections, "totalCount", &page->total) != JSON_OK) { return COLLECTION_INVALID; }
        page->name = collection_string(CGET(home, "greeting"), "transformedLabel");
    } else {
        yyjson_val *sections = CGET(data, "homeSections");
        if (!type_is(collection_string(sections, "__typename"), "HomeSectionCollection")) { return COLLECTION_ERROR; }
        yyjson_val *section = yyjson_arr_get(CGET(sections, "sections"), 0);
        if (!type_is(collection_string(section, "__typename"), "HomeSection")) { return COLLECTION_ERROR; }
        yyjson_val *items = CGET(section, "sectionItems");
        rows = CGET(items, "items");
        JsonResult total = json_view_uint(items, "totalCount", &page->total);
        if (total != JSON_OK && total != JSON_MISSING) { return COLLECTION_INVALID; }
        uint32_t next = 0;
        JsonResult cursor = json_view_uint(CGET(items, "pagingInfo"), "nextOffset", &next);
        if (cursor == JSON_OK) {
            if (next <= page->offset) { page->boundary = true; }
            else { page->next = next; }
        } else if (cursor != JSON_NULL) { return COLLECTION_INVALID; }
    }
    CollectionResult result = collection_rows(page, rows);
    if (result != COLLECTION_OK) { return result; }
    for (uint32_t i = 0; i < page->count; ++i) {
        yyjson_val *row = yyjson_arr_get(rows, i);
        if (page->kind == COLLECTION_HOME_SECTION) { page->items[i] = collection_home_item(row); }
        else {
            CollectionItem *item = &page->items[i];
            item->source = row; item->kind = COLLECTION_SHELF;
            item->id = item->uri = collection_string(row, "uri");
            yyjson_val *section_data = CGET(row, "data");
            item->discriminator = collection_string(section_data, "__typename");
            item->name = collection_string(CGET(section_data, "title"), "transformedLabel");
            item->available = yyjson_is_arr(CGET(CGET(row, "sectionItems"), "items"));
        }
    }
    return page->partial ? COLLECTION_PARTIAL : COLLECTION_OK;
}

CollectionResult collection_shelf_item(const CollectionPage *page, uint32_t shelf,
    uint32_t index, CollectionItem *output)
{
    if (output == NULL) { return COLLECTION_INVALID; }
    *output = (CollectionItem){0};
    if (page == NULL || page->kind != COLLECTION_HOME || shelf >= page->count) { return COLLECTION_INVALID; }
    yyjson_val *items = CGET(CGET(page->items[shelf].source, "sectionItems"), "items");
    if (index >= yyjson_arr_size(items)) { return COLLECTION_INVALID; }
    *output = collection_home_item(yyjson_arr_get(items, index)); return COLLECTION_OK;
}
