#include "support/http_server.h"
#include "lyrics_test.h"
#include "core/memory_budget.h"
#include <stdalign.h>
#include <wchar.h>

static alignas(16) unsigned char http_memory[MEMORY_BUDGET_HTTP], controls[LYRICS_MODEL_BUDGET];
static LyricsResult wait_document(LyricsService *service, HttpClient *http, const LyricsDocument **doc)
{
    uint64_t until = GetTickCount64() + 10000;
    LyricsResult result;
    do {
        CHECK(GetTickCount64() < until); result = lyrics_poll(service, doc);
        if (result == LYRICS_PENDING) { (void)http_wait(http, 1000); }
    } while (result == LYRICS_PENDING);
    return result;
}

void lyrics_http_tests(void)
{
    static const char body[] = LYRICS_BODY("LINE_SYNCED","NONE","[{\"startTimeMs\":\"0\",\"words\":\"network\"}]","[]");
    char headers[192]; CHECK(snprintf(headers, sizeof(headers), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", sizeof(body)-1) > 0);
    TestHttpResponse responses[] = {
        {.headers=headers,.body=body,.bytes=sizeof(body)-1},
        {.headers="HTTP/1.1 404 Not Found\r\nContent-Length: 8\r\nConnection: close\r\n\r\n",.body="not json",.bytes=8},
        {.headers="HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers="HTTP/1.1 429 Too Many Requests\r\nRetry-After: 0\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers="HTTP/1.1 302 Found\r\nLocation: https://untrusted.example/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers=headers,.body=body,.bytes=sizeof(body)-1,.hold_headers=true,.allow_disconnect=true},
        {.headers=headers,.body=body,.bytes=sizeof(body)-1},
        {.headers=headers,.body=body,.bytes=sizeof(body)-1,.hold_headers=true,.allow_disconnect=true}};
    Metrics metrics; CHECK(metrics_init(&metrics)); TestHttpServer server;
    CHECK(test_http_server_start(&server, &metrics, responses, 8));
    wchar_t origin[128]; CHECK(swprintf_s(origin, 128, L"http://127.0.0.1:%u", server.port) > 0);
    Arena http_arena; CHECK(arena_init(&http_arena, http_memory, sizeof(http_memory)));
    HttpConfig hc = {.allow_loopback_http=true,.header_origin_count=1,.metrics=&metrics}; hc.header_origins[0]=origin;
    HttpClient *http; CHECK(http_client_create(&http_arena, &hc, &http) == HTTP_OK);
    LyricsConfig cfg = {.http=http,.app_version=L"fixture-version",.accept_language=L"en-GB"}; LyricsService *service;
    CHECK(lyrics_service_init(controls, sizeof(controls), lt_pool, &cfg, &service) == LYRICS_INVALID);
    cfg.fixture_origin=L"http://localhost:80";
    CHECK(lyrics_service_init(controls, sizeof(controls), lt_pool, &cfg, &service) == LYRICS_INVALID);
    cfg.fixture_origin=origin;
    CHECK(lyrics_service_init(controls, sizeof(controls), lt_pool, &cfg, &service) == LYRICS_OK);
    LyricsTrack track = {.uri={"spotify:track:0000000000000000000001",36}, .artwork={"https://i.test/a b",18},
        .client_language={"all",3},.market={"NL",2},.vocal_removal=true};
    const LyricsDocument *doc; LyricsRequest request;
    for (unsigned i = 0; i < 5; ++i) {
        CHECK(lyrics_request(service, &track, i+1, &request) == LYRICS_PENDING);
        LyricsResult r = wait_document(service, http, &doc);
        CHECK(r == (i < 2 ? LYRICS_OK : i == 2 ? LYRICS_AUTH : i == 3 ? LYRICS_RATE_LIMIT : LYRICS_ERROR));
        if (i < 2) {
            CHECK(doc->availability == (i == 0 ? LYRICS_AVAILABLE : LYRICS_MISSING)); CHECK(lyrics_document_release(doc));
        } else { CHECK(doc == NULL); }
        CHECK(atomic_load(&server.accepted) == i+1); /* No retry/redirect request. */
    }
    CHECK(strstr(server.requests[0], "GET /color-lyrics/v2/track/0000000000000000000001/image/https%3A%2F%2Fi.test%2Fa%20b?format=json&vocalRemoval=true&clientLanguage=all&market=NL HTTP/1.1") != NULL);
    CHECK(strstr(server.requests[0], "Authorization: Bearer fixture") != NULL);
    CHECK(strstr(server.requests[0], "App-Platform: Win32_x86_64") != NULL);
    CHECK(strstr(server.requests[0], "Spotify-App-Version: fixture-version") != NULL);
    CHECK(strstr(server.requests[0], "Accept: application/json") != NULL);
    CHECK(strstr(server.requests[0], "client-token:") == NULL && strstr(server.requests[0], "Content-Type:") == NULL);
    ResetEvent(server.seen); ResetEvent(server.release_headers);
    CHECK(lyrics_request(service, &track, 6, &request) == LYRICS_PENDING);
    CHECK(WaitForSingleObject(server.seen, 5000) == WAIT_OBJECT_0);
    LyricsRequest newest; track.uri=(JsonString){"spotify:track:0000000000000000000002",36};
    CHECK(lyrics_request(service, &track, 7, &newest) == LYRICS_PENDING);
    CHECK(json_page_pool_resident(lt_pool) == 2); CHECK(SetEvent(server.release_headers));
    CHECK(wait_document(service, http, &doc) == LYRICS_STALE && doc == NULL);
    LyricsResult replacement = wait_document(service, http, &doc);
    if (replacement != LYRICS_OK || doc == NULL) {
        printf("replacement result=%u accepted=%u server-error=%lu\n", (unsigned)replacement,
            atomic_load(&server.accepted), (unsigned long)atomic_load(&server.error));
    }
    CHECK(replacement == LYRICS_OK && doc != NULL && doc->track_id[15] == 2);
    CHECK(lyrics_document_release(doc));
    ResetEvent(server.seen); ResetEvent(server.release_headers);
    CHECK(lyrics_request(service, &track, 8, &request) == LYRICS_PENDING);
    CHECK(WaitForSingleObject(server.seen, 5000) == WAIT_OBJECT_0);
    CHECK(lyrics_cancel(service, request.ticket) == LYRICS_PENDING && json_page_pool_resident(lt_pool) == 1);
    CHECK(lyrics_shutdown(service) == LYRICS_PENDING); CHECK(SetEvent(server.release_headers));
    CHECK(wait_document(service, http, &doc) == LYRICS_CANCELLED && doc == NULL);
    CHECK(lyrics_shutdown(service) == LYRICS_OK && json_page_pool_resident(lt_pool) == 0);
    CHECK(http_client_shutdown(http, 5000) == HTTP_OK); test_http_server_stop(&server);
    CHECK(metrics_balanced(&metrics)); arena_dispose(&http_arena);
}
