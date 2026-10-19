#include "lyrics_internal.h"
#include <string.h>

_Static_assert(sizeof(LyricsDocument) + JSON_PAGE_BODY_CAP <= LYRICS_DOCUMENT_CAP, "Lyrics document/text cap");
_Static_assert(sizeof(LyricsDocument) <= JSON_PAGE_POOL_CAP, "Compact lyrics reuse DOM memory");

static bool equal(JsonString value, const char *text)
{
    return json_span_equal(value, (JsonString){text, strlen(text)});
}
static bool timestamp(yyjson_val *row, bool timed, uint64_t *output)
{
    yyjson_val *value = yyjson_obj_get(row, "startTimeMs");
    if (value == NULL && !timed) { *output = 0; return true; }
    JsonString s;
    if (json_base_string(value, 20, &s) != JSON_OK || s.bytes == 0) { return false; }
    uint64_t number = 0;
    for (size_t i = 0; i < s.bytes; ++i) {
        unsigned digit = (unsigned char)s.data[i] - (unsigned)'0';
        if (digit > 9 || number > (UINT64_MAX - 1 - digit) / 10) { return false; }
        number = number * 10 + digit;
    }
    *output = number; return true;
}
static LyricsResult rows(yyjson_val *array, LyricsLine *lines, bool timed)
{
    size_t i, count; yyjson_val *row; uint64_t previous = 0;
    yyjson_arr_foreach(array, i, count, row) {
        if (!yyjson_is_obj(row) || !timestamp(row, timed, &lines[i].start_ms) ||
            (i != 0 && lines[i].start_ms < previous) ||
            json_view_string(row, "words", &lines[i].text) != JSON_OK) { return LYRICS_INVALID; }
        previous = lines[i].start_ms;
    }
    return LYRICS_OK;
}

LyricsResult lyrics_parse(JsonPage *page, const uint8_t track[16], size_t bytes,
    bool missing, const LyricsDocument **output)
{
    if (bytes > JSON_PAGE_BODY_CAP || bytes > LYRICS_DOCUMENT_CAP - sizeof(LyricsDocument)) { return LYRICS_TOO_LARGE; }
    LyricsDocument model = {0}; model.owner = page; model.generation = page->generation;
    memcpy(model.track_id, track, 16);
    if (missing) { model.availability = LYRICS_MISSING; }
    else {
        JsonResult r = json_page_parse(page, bytes);
        if (r != JSON_OK) { return r == JSON_NO_MEMORY ? LYRICS_NO_MEMORY : r == JSON_TOO_LARGE ? LYRICS_TOO_LARGE : LYRICS_INVALID; }
        yyjson_val *lyrics = yyjson_obj_get(yyjson_doc_get_root(page->doc), "lyrics");
        if (!yyjson_is_obj(lyrics)) { return LYRICS_INVALID; }
        if (json_view_string(lyrics, "syncType", &model.raw_sync) != JSON_OK ||
            json_view_string(lyrics, "capStatus", &model.cap_status) != JSON_OK ||
            json_view_string(lyrics, "provider", &model.provider) != JSON_OK ||
            json_view_string(lyrics, "providerDisplayName", &model.provider_display_name) != JSON_OK ||
            json_view_string(lyrics, "providerLyricsId", &model.provider_id) != JSON_OK) { return LYRICS_INVALID; }
        model.sync = equal(model.raw_sync, "LINE_SYNCED") ? LYRICS_LINE_SYNCED :
            equal(model.raw_sync, "SYLLABLE_SYNCED") ? LYRICS_SYLLABLE_SYNCED : LYRICS_PLAIN;
        JsonResult language = json_view_string(lyrics, "language", &model.language);
        if (language != JSON_OK && language != JSON_MISSING) { return LYRICS_INVALID; }
        yyjson_val *flag = yyjson_obj_get(lyrics, "isRtlLanguage");
        if (flag != NULL && json_base_bool(flag, &model.rtl) != JSON_OK) { return LYRICS_INVALID; }
        flag = yyjson_obj_get(yyjson_doc_get_root(page->doc), "hasVocalRemoval");
        if (flag != NULL && json_base_bool(flag, &model.vocal_removal) != JSON_OK) { return LYRICS_INVALID; }
        yyjson_val *full = yyjson_obj_get(lyrics, "lines"), *preview = yyjson_obj_get(lyrics, "previewLines");
        if (!yyjson_is_arr(full) || !yyjson_is_arr(preview)) { return LYRICS_INVALID; }
        size_t count = yyjson_arr_size(full), previews = yyjson_arr_size(preview);
        if (count > LYRICS_LINE_CAP || previews > LYRICS_LINE_CAP - count) { return LYRICS_TOO_LARGE; }
        bool capped = !equal(model.cap_status, "NONE");
        model.availability = capped ? LYRICS_CAPPED : count == 0 ? LYRICS_EMPTY : LYRICS_AVAILABLE;
        model.count = capped ? 0 : (uint32_t)count; model.preview_count = (uint32_t)previews;
        LyricsResult parsed = rows(full, model.lines, model.sync != LYRICS_PLAIN);
        if (parsed != LYRICS_OK) { return parsed; }
        if (capped) { memset(model.lines, 0, sizeof(model.lines)); }
        parsed = rows(preview, model.lines + model.count, model.sync != LYRICS_PLAIN);
        if (parsed != LYRICS_OK) { return parsed; }
    }
    void *compact = NULL;
    if (json_page_materialize(page, &model, sizeof(model), &compact) != JSON_OK) { return LYRICS_NO_MEMORY; }
    *output = compact; return LYRICS_OK;
}
