#include "lyrics_test.h"

static void timestamps(void)
{
    const char *rows[] = {
        "[{\"startTimeMs\":\"18446744073709551616\",\"words\":\"\"}]",
        "[{\"startTimeMs\":\"18446744073709551615\",\"words\":\"\"}]",
        "[{\"startTimeMs\":\"-1\",\"words\":\"\"}]", "[{\"startTimeMs\":\"\",\"words\":\"\"}]",
        "[{\"startTimeMs\":\"1e3\",\"words\":\"\"}]", "[{\"startTimeMs\":1,\"words\":\"\"}]",
        "[{\"words\":\"missing\"}]", "[{\"startTimeMs\":\"0\",\"words\":null}]",
        "[{\"startTimeMs\":\"20\",\"words\":\"a\"},{\"startTimeMs\":\"10\",\"words\":\"b\"}]"};
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); ++i) {
        LyricsRequest r = lt_request(30, 1); const LyricsDocument *doc;
        char body[1024]; CHECK(snprintf(body, sizeof(body), LYRICS_BODY("LINE_SYNCED","NONE","%s","[]"), rows[i]) > 0);
        CHECK(lt_deliver(r, body, &doc) == LYRICS_INVALID && doc == NULL);
        CHECK(json_page_pool_resident(lt_pool) == 0);
    }
    LyricsRequest r = lt_request(30, 1); const LyricsDocument *doc;
    CHECK(lt_deliver(r, "{", &doc) == LYRICS_INVALID && doc == NULL);
    r = lt_request(30, 1);
    CHECK(lt_deliver(r, LYRICS_BODY("LINE_SYNCED","NONE","[{\"startTimeMs\":\"0\",\"words\":\"\xff\"}]","[]"), &doc) == LYRICS_INVALID);
    r = lt_request(30, 1);
    CHECK(lt_deliver(r, "{\"lyrics\":{\"syncType\":\"LINE_SYNCED\",\"capStatus\":\"NONE\",\"provider\":\"x\","
        "\"providerDisplayName\":\"x\",\"providerLyricsId\":\"x\",\"language\":false,\"lines\":[],\"previewLines\":[]}}", &doc) == LYRICS_INVALID);
}

static void limits(void)
{
    const LyricsDocument *doc;
    for (unsigned count = 2048; count <= 2049; ++count) {
        LyricsRequest r = lt_request(31 + count - 2048, 1);
        size_t used = (size_t)snprintf(r.body, r.capacity, "{\"lyrics\":{\"syncType\":\"LINE_SYNCED\",\"capStatus\":\"NONE\","
            "\"provider\":\"x\",\"providerDisplayName\":\"x\",\"providerLyricsId\":\"x\",\"lines\":[");
        for (unsigned i = 0; i < count; ++i) {
            const char *line = i ? ",{\"startTimeMs\":\"0\",\"words\":\"\"}" : "{\"startTimeMs\":\"0\",\"words\":\"\"}";
            size_t bytes = strlen(line); CHECK(used + bytes < r.capacity); memcpy(r.body + used, line, bytes); used += bytes;
        }
        const char *end = "],\"previewLines\":[]}}"; CHECK(used + strlen(end) < r.capacity);
        memcpy(r.body + used, end, strlen(end)); used += strlen(end);
        LyricsResult result = lyrics_complete(lt_service, r.ticket, LYRICS_OK, 200, used, &doc);
        CHECK(result == (count == 2048 ? LYRICS_OK : LYRICS_TOO_LARGE));
        if (doc != NULL) {
            CHECK(doc->count == 2048 && lyrics_line_at(doc, 0) == 2047);
            CHECK(sizeof(*doc) + used <= LYRICS_DOCUMENT_CAP); CHECK(lyrics_document_release(doc));
        }
    }
    CHECK(json_page_pool_resident(lt_pool) == 0);
    LyricsRequest r = lt_request(33, 1);
    CHECK(lyrics_complete(lt_service, r.ticket, LYRICS_OK, 200, SIZE_MAX, &doc) == LYRICS_TOO_LARGE);
    r = lt_request(33, 1);
    r.body[0] = '[';
    for (size_t i = 1; i < 60000; i += 2) { r.body[i] = '0'; r.body[i+1] = ','; } r.body[60000] = ']';
    CHECK(lyrics_complete(lt_service, r.ticket, LYRICS_OK, 200, 60001, &doc) == LYRICS_NO_MEMORY);
    CHECK(json_page_pool_resident(lt_pool) == 0);
}

static void generations_and_statuses(void)
{
    const LyricsDocument *doc; LyricsRequest old = lt_request(40, 1), current = lt_request(41, 2), third;
    LyricsTrack track = {.uri = {"spotify:track:0000000000000000000003",36}, .artwork = {"art",3}};
    CHECK(lyrics_request(lt_service, &track, 42, &third) == LYRICS_FULL && third.body == NULL);
    CHECK(json_page_pool_resident(lt_pool) == 2);
    CHECK(lyrics_complete(lt_service, old.ticket, LYRICS_OK, 200, 0, &doc) == LYRICS_STALE && doc == NULL);
    CHECK(lyrics_complete(lt_service, current.ticket, LYRICS_OK, 200, 0, &doc) == LYRICS_STALE);
    current = lt_request(43, 2);
    CHECK(lyrics_cancel(lt_service, current.ticket) == LYRICS_PENDING && json_page_pool_resident(lt_pool) == 1);
    CHECK(lyrics_complete(lt_service, current.ticket, LYRICS_OK, 404, 0, &doc) == LYRICS_CANCELLED && doc == NULL);
    CHECK(lyrics_complete(lt_service, old.ticket, LYRICS_CANCELLED, 0, 0, &doc) == LYRICS_STALE);
    unsigned statuses[] = {401,403,429,500};
    for (size_t i = 0; i < 4; ++i) {
        current = lt_request(44, 2);
        CHECK(lyrics_complete(lt_service, current.ticket, LYRICS_OK, statuses[i], 0, &doc) ==
            (i < 2 ? LYRICS_AUTH : i == 2 ? LYRICS_RATE_LIMIT : LYRICS_ERROR));
        CHECK(doc == NULL);
    }
    current = lt_request(45, 2); memcpy(current.body, "not json", 8);
    CHECK(lyrics_complete(lt_service, current.ticket, LYRICS_OK, 404, 8, &doc) == LYRICS_OK);
    CHECK(doc->availability == LYRICS_MISSING && lyrics_document_release(doc));
    CHECK(lyrics_reset(lt_service, 46) == LYRICS_OK && json_page_pool_resident(lt_pool) == 0);
    track.uri = (JsonString){"bad",3}; CHECK(lyrics_request(lt_service, &track, 47, &third) == LYRICS_INVALID);
    track.uri = (JsonString){"spotify:track:0000000000000000000001",36}; track.artwork = (JsonString){0};
    CHECK(lyrics_request(lt_service, &track, 47, &third) == LYRICS_INVALID);
    CHECK(!lyrics_document_retain(NULL) && !lyrics_document_release(NULL));
}
void lyrics_error_tests(void) { timestamps(); limits(); generations_and_statuses(); }
