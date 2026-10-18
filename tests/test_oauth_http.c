#include "oauth_test.h"
#include "support/http_server.h"
#include "core/memory_budget.h"
#include <wchar.h>

static _Alignas(16) uint8_t http_memory[MEMORY_BUDGET_HTTP];
static Arena http_arena;
static Metrics metrics;
static void headers(char out[512], size_t bytes, const char *status)
{
    CHECK(sprintf_s(out, 512, "%sContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", status, bytes) > 0);
}
static HttpClient *open_http(TestHttpServer *server, wchar_t url[128])
{
    CHECK(arena_init(&http_arena, http_memory, sizeof(http_memory)));
    wchar_t origin[128];
    CHECK(swprintf_s(origin, 128, L"http://127.0.0.1:%u", (unsigned)server->port) > 0);
    CHECK(swprintf_s(url, 128, L"%ls/api/token", origin) > 0);
    HttpConfig c = {.allow_loopback_http = true, .header_origin_count = 1, .metrics = &metrics};
    c.header_origins[0] = origin; HttpClient *http;
    CHECK(http_client_create(&http_arena, &c, &http) == HTTP_OK); return http;
}
static void close_http(HttpClient *http, TestHttpServer *server)
{
    CHECK(http_client_shutdown(http, 5000) == HTTP_OK);
    test_http_server_stop(server); arena_dispose(&http_arena); CHECK(metrics_balanced(&metrics));
}
static void complete(OAuthFlow *f, HttpClient *http)
{
    uint64_t until = GetTickCount64() + 5000;
    while (f->http_active) {
        CHECK(GetTickCount64() < until);
        (void)oauth_poll(f);
        if (f->http_active) { CHECK(http_wait(http, 1000) == HTTP_OK); }
    }
}
static const char initial_json[] = "{\"access_token\":\"access-fixture\",\"token_type\":\"Bearer\",\"expires_in\":3600,"
    "\"refresh_token\":\"refresh-fixture\",\"scope\":\"streaming user-read-private\"}";
static void exchange(void)
{
    CHECK(metrics_init(&metrics)); TestHttpServer server;
    char raw[512]; headers(raw, sizeof(initial_json) - 1, "HTTP/1.1 200 OK\r\n");
    TestHttpResponse response = {.headers = raw, .body = initial_json,
        .bytes = sizeof(initial_json) - 1, .fragment = 1, .capture_upload = true};
    CHECK(test_http_server_start(&server, &metrics, &response, 1));
    wchar_t url[128]; HttpClient *http = open_http(&server, url);
    OAuthFlow *f = oauth_test_open(url); oauth_test_code(f);
    char verifier[129]; memcpy(verifier, f->verifier, sizeof(verifier));
    CHECK(oauth_exchange(f, http) == OAUTH_OK && oauth_exchange(f, http) == OAUTH_INVALID);
    CHECK(oauth_test_zero(f->verifier, sizeof(f->verifier)));
    complete(f, http); CHECK(oauth_state(f) == OAUTH_DONE && oauth_result(f) == OAUTH_OK);
    CHECK(atomic_load(&server.accepted) == 1 && strstr(server.requests[0], "POST /api/token HTTP/1.1") != NULL);
    CHECK(strstr(server.requests[0], "Content-Type: application/x-www-form-urlencoded") != NULL);
    CHECK(strstr(server.requests[0], "grant_type=authorization_code") != NULL && strstr(server.requests[0], verifier) != NULL);
    CHECK(strstr(server.requests[0], "code=auth%2Bcode") != NULL && strstr(server.requests[0], "client_secret") == NULL);
    CHECK(strstr(server.requests[0], "Authorization:") == NULL && strstr(server.requests[0], "redirect_uri=http%3A%2F%2F127.0.0.1%3A") != NULL);
    CredentialRecord record; CHECK(credential_record_init(&record, "fixture-client") == CREDENTIAL_OK);
    char device[41]; memcpy(device, record.device_id, sizeof(device)); OAuthToken token;
    CHECK(oauth_take_token(f, &record, &token) == OAUTH_OK);
    CHECK(strcmp(record.refresh_token, "refresh-fixture") == 0 && strcmp(record.device_id, device) == 0);
    CHECK(strcmp(token.access, "access-fixture") == 0 && token.expires_at == oauth_test_now + 3600000);
    CHECK(oauth_scope_granted(&token, "streaming") && !oauth_scope_granted(&token, "stream") && !oauth_scope_granted(&token, "private-scope"));
    CHECK(oauth_take_token(f, &record, &token) == OAUTH_REPLAY);
    SecureZeroMemory(&token, sizeof(token)); credential_record_clear(&record); oauth_test_close(f); close_http(http, &server);
}
static void refresh_cases(void)
{
    const char *bodies[] = {
        "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":10}",
        "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":10,\"refresh_token\":\"rotated\"}",
        "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":0,\"refresh_token\":\"bad-rotation\"}",
        "{\"error\":\"invalid_grant\"}",
        "{\"access_token\":\"new-access\",\"token_type\":\"Basic\",\"expires_in\":10}",
        "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":\"10\"}",
        "{\"access_token\":null,\"token_type\":\"Bearer\",\"expires_in\":10}",
        "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":10,\"refresh_token\":null}",
        "{\"access_token\":\"first\",\"access_token\":\"second\",\"token_type\":\"Bearer\",\"expires_in\":10}",
        "{\"access_token\":\"a\\u0000b\",\"token_type\":\"Bearer\",\"expires_in\":10}",
        "{\"access_token\":\"a\\nb\",\"token_type\":\"Bearer\",\"expires_in\":10}",
        "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":10,\"scope\":null}",
        "{\"access_token\":\"new-access\",\"token_type\":\"Bearer\",\"expires_in\":10,\"scope\":\"a\\nb\"}",
        "not json"
    };
    for (size_t i = 0; i < sizeof(bodies) / sizeof(bodies[0]); ++i) {
        CHECK(metrics_init(&metrics)); TestHttpServer server;
        char raw[512]; headers(raw, strlen(bodies[i]), "HTTP/1.1 200 OK\r\n");
        TestHttpResponse response = {.headers = raw, .body = bodies[i], .bytes = strlen(bodies[i]), .capture_upload = true};
        CHECK(test_http_server_start(&server, &metrics, &response, 1));
        wchar_t url[128]; HttpClient *http = open_http(&server, url); OAuthFlow *f = oauth_test_open(url);
        CredentialRecord record; CHECK(credential_record_init(&record, "fixture-client") == CREDENTIAL_OK);
        memcpy(record.refresh_token, "old+refresh", 12); memcpy(record.username, "canonical", 10);
        record.ap_type = 2; record.ap_bytes = 3; memcpy(record.ap_blob, "abc", 3); CredentialRecord before = record;
        CredentialRecord wrong = record; memcpy(wrong.client_id, "wrong-client", 13);
        CHECK(oauth_refresh(f, http, &wrong) == OAUTH_INVALID && oauth_balanced(f)); credential_record_clear(&wrong);
        CHECK(oauth_refresh(f, http, &record) == OAUTH_OK); complete(f, http);
        CHECK(atomic_load(&server.accepted) == 1 && strstr(server.requests[0], "grant_type=refresh_token") != NULL && strstr(server.requests[0], "refresh_token=old%2Brefresh") != NULL);
        OAuthToken token = {0};
        if (i < 2) {
            CHECK(oauth_result(f) == OAUTH_OK && memcmp(&record, &before, sizeof(record)) == 0);
            CHECK(oauth_take_token(f, &record, &token) == OAUTH_OK);
            CHECK(strcmp(record.refresh_token, i == 0 ? "old+refresh" : "rotated") == 0);
            CHECK(strcmp(record.username, before.username) == 0 && strcmp(record.device_id, before.device_id) == 0 && record.ap_type == 2);
        } else { CHECK(oauth_result(f) == OAUTH_BAD_RESPONSE && memcmp(&record, &before, sizeof(record)) == 0); }
        SecureZeroMemory(&token, sizeof(token)); credential_record_clear(&record); credential_record_clear(&before);
        oauth_test_close(f); close_http(http, &server);
    }
}
static void response_bounds_and_expiry(void)
{
    char body[18000];
    for (unsigned mode = 0; mode < 5; ++mode) {
        size_t bytes;
        if (mode == 0) { memcpy(body, initial_json, sizeof(initial_json)); bytes = sizeof(initial_json) - 1; }
        else if (mode == 1) {
            const char *missing = "{\"access_token\":\"access\",\"token_type\":\"Bearer\",\"expires_in\":10}";
            bytes = strlen(missing); memcpy(body, missing, bytes + 1);
        } else if (mode == 2) {
            memcpy(body, "{\"access_token\":\"", 17); memset(body + 17, 'a', 1025);
            const char *tail = "\",\"token_type\":\"Bearer\",\"expires_in\":10,\"refresh_token\":\"r\"}";
            memcpy(body + 1042, tail, strlen(tail) + 1); bytes = 1042 + strlen(tail);
        } else { memset(body, ' ', sizeof(body)); bytes = sizeof(body); }
        CHECK(metrics_init(&metrics)); TestHttpServer server; char raw[512]; headers(raw, bytes, "HTTP/1.1 200 OK\r\n");
        TestHttpResponse response = {.headers = raw, .body = body, .bytes = bytes, .hold_body = mode == 4, .capture_upload = true};
        CHECK(test_http_server_start(&server, &metrics, &response, 1)); wchar_t url[128]; HttpClient *http = open_http(&server, url);
        OAuthFlow *f = oauth_test_open(url); oauth_test_code(f); CHECK(oauth_exchange(f, http) == OAUTH_OK);
        if (mode == 4) {
            CHECK(WaitForSingleObject(server.headers_sent, 3000) == WAIT_OBJECT_0);
            oauth_test_now += 180000; (void)oauth_poll(f); CHECK(oauth_state(f) == OAUTH_TERMINAL && oauth_result(f) == OAUTH_EXPIRED);
            CHECK(SetEvent(server.release_body));
        }
        complete(f, http);
        if (mode == 0) {
            CredentialRecord record; OAuthToken token = {0}; CHECK(credential_record_init(&record, "fixture-client") == CREDENTIAL_OK);
            oauth_test_now += 3600000; CHECK(oauth_take_token(f, &record, &token) == OAUTH_EXPIRED && record.refresh_token[0] == 0);
            credential_record_clear(&record);
        } else { CHECK(oauth_result(f) == (mode == 4 ? OAUTH_EXPIRED : mode == 3 ? OAUTH_HTTP_ERROR : OAUTH_BAD_RESPONSE)); }
        oauth_test_close(f); close_http(http, &server);
    }
}
static void cancellation_and_errors(void)
{
    CHECK(metrics_init(&metrics)); TestHttpServer server;
    char raw[512]; headers(raw, sizeof(initial_json) - 1, "HTTP/1.1 200 OK\r\n");
    TestHttpResponse response = {.headers = raw, .body = initial_json,
        .bytes = sizeof(initial_json) - 1, .hold_body = true, .capture_upload = true};
    CHECK(test_http_server_start(&server, &metrics, &response, 1)); wchar_t url[128]; HttpClient *http = open_http(&server, url);
    OAuthFlow *f = oauth_test_open(url); oauth_test_code(f); CHECK(oauth_exchange(f, http) == OAUTH_OK);
    CHECK(WaitForSingleObject(server.headers_sent, 3000) == WAIT_OBJECT_0);
    CHECK(oauth_cancel(f) == OAUTH_CANCELLED && f->http_active);
    CHECK(!oauth_test_zero(f->upload, sizeof(f->upload))); /* borrowed through final consume */
    CHECK(SetEvent(server.release_body)); complete(f, http);
    CHECK(oauth_result(f) == OAUTH_CANCELLED && oauth_test_zero(f->upload, sizeof(f->upload)));
    oauth_test_close(f); close_http(http, &server);
    const char *statuses[] = {"HTTP/1.1 400 Bad Request\r\n", "HTTP/1.1 302 Found\r\nLocation: https://evil.invalid/\r\n"};
    for (size_t i = 0; i < 2; ++i) {
        CHECK(metrics_init(&metrics)); headers(raw, response.bytes, statuses[i]); response.hold_body = false;
        CHECK(test_http_server_start(&server, &metrics, &response, 1)); http = open_http(&server, url);
        f = oauth_test_open(url); oauth_test_code(f); CHECK(oauth_exchange(f, http) == OAUTH_OK); complete(f, http);
        CHECK(oauth_result(f) == OAUTH_HTTP_ERROR && atomic_load(&server.accepted) == 1);
        oauth_test_close(f); close_http(http, &server);
    }
}
void test_oauth_http(void) { exchange(); refresh_cases(); cancellation_and_errors(); response_bounds_and_expiry(); }
