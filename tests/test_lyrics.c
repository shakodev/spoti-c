#include "lyrics_test.h"
#include "core/memory_budget.h"
#include "api/collection_api.h"
#include "api/catalog_api.h"
#include <stdalign.h>
#include <wchar.h>

static alignas(16) unsigned char pool_memory[MEMORY_BUDGET_API_PAGES], model_memory[LYRICS_MODEL_BUDGET];
LyricsService *lt_service;
JsonPagePool *lt_pool;

LyricsRequest lt_request(uint64_t generation, unsigned track)
{
    char uri[37]; CHECK(snprintf(uri, sizeof(uri), "spotify:track:000000000000000000000%u", track) == 36);
    LyricsTrack t = {.uri = lyric_span(uri), .artwork = lyric_span("https://image.test/a b?x=1&y=2")};
    LyricsRequest request;
    CHECK(lyrics_request(lt_service, &t, generation, &request) == LYRICS_PENDING && request.body != NULL);
    CHECK(request.contract == contract_inspect(API_LYRICS) && request.contract->persisted_sha256 == NULL);
    return request;
}
LyricsResult lt_deliver(LyricsRequest r, const char *json, const LyricsDocument **output)
{
    size_t bytes = strlen(json); CHECK(bytes <= r.capacity); memcpy(r.body, json, bytes);
    return lyrics_complete(lt_service, r.ticket, LYRICS_OK, 200, bytes, output);
}
LyricsResult lt_fixture(LyricsRequest r, const char *name, const LyricsDocument **output)
{
    char path[512]; CHECK(snprintf(path, sizeof(path), "%s/%s.json", API_FIXTURE_DIR, name) > 0);
    yyjson_doc *doc = yyjson_read_file(path, 0, NULL, NULL); CHECK(doc != NULL);
    yyjson_val *response = yyjson_obj_get(yyjson_doc_get_root(doc), "response");
    unsigned status = (unsigned)yyjson_get_uint(yyjson_obj_get(response, "status"));
    size_t bytes; char *json = yyjson_val_write(yyjson_obj_get(response, "body"), 0, &bytes);
    CHECK(json != NULL && bytes <= r.capacity); memcpy(r.body, json, bytes); free(json); yyjson_doc_free(doc);
    return lyrics_complete(lt_service, r.ticket, LYRICS_OK, status, bytes, output);
}

static void fixtures(void)
{
    const LyricsDocument *doc;
    LyricsRequest r = lt_request(1, 1);
    CHECK(wcsstr(r.url, L"https://spclient.wg.spotify.com/color-lyrics/v2/track/0000000000000000000001/image/") == r.url);
    CHECK(wcsstr(r.url, L"https%3A%2F%2Fimage.test%2Fa%20b%3Fx%3D1%26y%3D2?format=json&vocalRemoval=false") != NULL);
    CHECK(lt_fixture(r, "lyrics-observed-positive", &doc) == LYRICS_OK);
    CHECK(doc->count == 104 && doc->preview_count == 5 && doc->availability == LYRICS_AVAILABLE);
    CHECK(doc->sync == LYRICS_LINE_SYNCED && doc->lines[0].start_ms == 30370 && doc->lines[103].start_ms == 318340);
    CHECK(doc->lines[103].text.bytes == 0 && json_span_equal(doc->language, lyric_span("redacted")));
    CHECK(doc->owner->doc == NULL && doc == doc->owner->pool);
    CHECK(lyrics_line_at(doc, 30369) == LYRICS_NO_LINE && lyrics_line_at(doc, 30370) == 0);
    CHECK(lyrics_line_at(doc, 33830) == 1 && lyrics_line_at(doc, 999999) == 103);
    CHECK(lyrics_document_release(doc));
    r = lt_request(2, 2);
    CHECK(lt_fixture(r, "lyrics-observed-error", &doc) == LYRICS_OK);
    CHECK(doc->availability == LYRICS_MISSING && doc->count == 0 && lyrics_line_at(doc, 10000) == LYRICS_NO_LINE);
    CHECK(lyrics_document_release(doc));
    CHECK(lyrics_reset(lt_service, 3) == LYRICS_OK && json_page_pool_resident(lt_pool) == 0);
}

static void modes(void)
{
    /* Local fixtures exercise source branches; only LINE_SYNCED/NONE has an
     * independently observed success in the predecessor capture. */
    const LyricsDocument *doc; LyricsRequest r = lt_request(10, 1);
    PlayerSnapshot playing = {.rate=48000,.state=PLAYER_PLAYING,.timeline_valid=true}; playing.track_id[15]=1;
    CHECK(lt_deliver(r, LYRICS_BODY("SYLLABLE_SYNCED","NONE",
        "[{\"startTimeMs\":\"10\",\"words\":\"one\"},{\"startTimeMs\":\"10\",\"words\":\"two\"},{\"startTimeMs\":\"20\",\"words\":\"\"}]", "[]"), &doc) == LYRICS_OK);
    CHECK(doc->sync == LYRICS_SYLLABLE_SYNCED && lyrics_line_at(doc, 10) == 1 && lyrics_line_at(doc, 20) == 2);
    CHECK(lyrics_document_release(doc));
    r = lt_request(11, 1);
    CHECK(lt_deliver(r, LYRICS_BODY("UNSYNCED","NONE",
        "[{\"words\":\"\\u65e5\\u672c \\ud83c\\udfb5\"},{\"words\":\"\"}]", "[]"), &doc) == LYRICS_OK);
    CHECK(doc->sync == LYRICS_PLAIN && doc->count == 2 && lyrics_line_at(doc, 0) == LYRICS_NO_LINE);
    CHECK(doc->lines[0].text.bytes == 11 && json_span_equal(doc->raw_sync, lyric_span("UNSYNCED")));
    CHECK(lyrics_next_deadline(doc, &playing, true, 1234) == CORE_CLOCK_NEVER);
    CHECK(lyrics_document_release(doc));
    r = lt_request(12, 1);
    CHECK(lt_deliver(r, LYRICS_BODY("FUTURE_MODE","FUTURE_CAP",
        "[{\"words\":\"full\"}]", "[{\"words\":\"preview\"}]"), &doc) == LYRICS_OK);
    CHECK(doc->availability == LYRICS_CAPPED && doc->count == 0 && doc->preview_count == 1);
    CHECK(json_span_equal(doc->lines[0].text, lyric_span("preview")) && doc->lines[1].text.data == NULL);
    CHECK(lyrics_next_deadline(doc, &playing, true, 1234) == CORE_CLOCK_NEVER);
    CHECK(lyrics_document_release(doc));
    r = lt_request(13, 1);
    CHECK(lt_deliver(r, LYRICS_BODY("LINE_SYNCED","NONE","[]","[]"), &doc) == LYRICS_OK);
    CHECK(doc->availability == LYRICS_EMPTY); CHECK(lyrics_document_release(doc));
    CHECK(lyrics_reset(lt_service, 14) == LYRICS_OK && json_page_pool_resident(lt_pool) == 0);
}

static DWORD WINAPI run(void *context)
{
    (void)context;
    CHECK(json_page_pool_size() <= sizeof(pool_memory));
    CHECK(lyrics_service_size() + catalog_service_size() + collection_service_size() <= MEMORY_BUDGET_MODEL);
    CHECK(json_page_pool_init(pool_memory, sizeof(pool_memory), &lt_pool) == JSON_OK);
    LyricsConfig config = {0};
    CHECK(lyrics_service_init(model_memory, sizeof(model_memory), lt_pool, &config, &lt_service) == LYRICS_OK);
    fixtures(); modes(); lyrics_error_tests(); lyrics_lifetime_tests(); lyrics_timeline_tests(); lyrics_http_tests();
    CHECK(lyrics_shutdown(lt_service) == LYRICS_OK && json_page_pool_resident(lt_pool) == 0);
    printf("Lyrics: compact document %zu bytes; controls %zu bytes; shared pool %zu bytes\n",
        sizeof(LyricsDocument), lyrics_service_size(), json_page_pool_size()); return 0;
}
int main(void)
{
    HANDLE worker = CreateThread(NULL, 0, run, NULL, 0, NULL); CHECK(worker != NULL);
    CHECK(WaitForSingleObject(worker, 55000) == WAIT_OBJECT_0); CHECK(CloseHandle(worker)); return 0;
}
