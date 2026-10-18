#include "token_test.h"
#include "core/memory_budget.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

TokenFixture token_test;
static _Alignas(TokenManager) uint8_t secret_memory[TOKEN_SECRET_LIMIT];
static _Alignas(16) uint8_t http_memory[MEMORY_BUDGET_HTTP];
static uint64_t now(void *context)
{
    TokenFixture *t = context; uint64_t value = t->now; t->now += t->clock_step; return value;
}
bool token_test_zero(const void *bytes, size_t size)
{
    const uint8_t *p = bytes;
    for (size_t i = 0; i < size; ++i) { if (p[i] != 0) { return false; } } return true;
}
void token_test_open(const TestHttpResponse *responses, size_t count)
{
    TokenFixture *t = &token_test; *t = (TokenFixture){0}; t->now = 1000; CHECK(count <= 8);
    CHECK(metrics_init(&t->metrics));
    for (size_t i = 0; i < count; ++i) {
        t->responses[i] = responses[i]; t->responses[i].capture_upload = true;
        CHECK(sprintf_s(t->headers[i], sizeof(t->headers[i]), "%sContent-Type: application/x-protobuf\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
            responses[i].headers == NULL ? "HTTP/1.1 200 OK\r\n" : responses[i].headers, responses[i].bytes) > 0);
        t->responses[i].headers = t->headers[i];
    }
    CHECK(test_http_server_start(&t->server, &t->metrics, t->responses, count));
    CHECK(swprintf_s(t->origin, 128, L"http://127.0.0.1:%u", (unsigned)t->server.port) > 0);
    CHECK(swprintf_s(t->client_url, 128, L"%ls/clienttoken", t->origin) > 0 && swprintf_s(t->login_url, 128, L"%ls/login5", t->origin) > 0);
    CHECK(arena_init(&t->secret, secret_memory, sizeof(secret_memory)) && arena_init(&t->transport, http_memory, sizeof(http_memory)));
    CHECK(metrics_attach_arena(&t->metrics, &t->secret, &t->secret_binding) && metrics_attach_arena(&t->metrics, &t->transport, &t->http_binding));
    HttpConfig hc = {.metrics = &t->metrics, .allow_loopback_http = true, .header_origin_count = 1}; hc.header_origins[0] = t->origin;
    CHECK(http_client_create(&t->transport, &hc, &t->http) == HTTP_OK);
    CHECK(credential_record_init(&t->record, "fixture-client") == CREDENTIAL_OK);
    memcpy(t->record.device_id, "0123456789abcdef0123456789abcdef01234567", 41);
    memcpy(t->record.refresh_token, "fixture-refresh", 16); memcpy(t->record.username, "fixture-user", 13);
    t->record.ap_type = 1; t->record.ap_bytes = 21; memcpy(t->record.ap_blob, "fixture-ap-credential", 21);
    t->config = (TokenConfig){.http = t->http, .credentials = &t->record, .client_id = "fixture-client", .client_version = "0.1.0-test",
        .clock = {now, t}, .windows_major = 10, .windows_build = 26100,
        .fixture_clienttoken_url = t->client_url, .fixture_login5_url = t->login_url, .service_origin_count = 3};
    t->config.service_origins[0] = L"https://api-partner.spotify.com";
    t->config.service_origins[1] = L"https://spclient.wg.spotify.com";
    t->config.service_origins[2] = L"https://dealer.spotify.com";
    CHECK(token_manager_create(&t->secret, &t->config, &t->manager) == TOKEN_OK);
    CHECK(t->secret.used == sizeof(TokenManager) && t->secret.used + PROTO_MAX_BLOB <= TOKEN_SECRET_LIMIT);
    CHECK(token_next_wait_ms(t->manager) == INFINITE && token_wait_handle(t->manager) == NULL);
}
TokenResult token_test_finish(void)
{
    TokenFixture *t = &token_test; uint64_t until = GetTickCount64() + 8000;
    for (;;) {
        CHECK(GetTickCount64() < until);
        TokenResult result = token_poll(t->manager);
        if (!t->manager->http_active && t->manager->stage != TS_CLIENT_HASH && t->manager->stage != TS_LOGIN_HASH) { return result; }
        HANDLE handle = token_wait_handle(t->manager); uint32_t wait = token_next_wait_ms(t->manager);
        if (wait > 1000) { wait = 1000; }
        if (handle != NULL) { CHECK(WaitForSingleObject(handle, wait) != WAIT_FAILED); }
        else { CHECK(wait == 0); }
    }
}
void token_test_close(void)
{
    TokenFixture *t = &token_test; uint64_t until = GetTickCount64() + 5000;
    while (token_manager_shutdown(t->manager) == TOKEN_PENDING) {
        CHECK(GetTickCount64() < until && http_wait(t->http, 1000) == HTTP_OK);
    }
    CHECK(token_balanced(t->manager));
    CHECK(token_test_zero(t->manager->slots, sizeof(t->manager->slots)) && token_test_zero(t->manager->response, sizeof(t->manager->response)) &&
        token_test_zero(t->manager->upload, sizeof(t->manager->upload)) && token_test_zero(&t->manager->scratch, sizeof(t->manager->scratch)) &&
        token_test_zero(t->manager->context, sizeof(t->manager->context)) && token_test_zero(t->manager->challenge_state, sizeof(t->manager->challenge_state)));
    CHECK(http_client_shutdown(t->http, 5000) == HTTP_OK); test_http_server_stop(&t->server);
    credential_record_clear(&t->record); arena_dispose(&t->secret); arena_dispose(&t->transport); CHECK(metrics_balanced(&t->metrics));
}
void token_test_request(unsigned index, const void *body, size_t bytes, bool login)
{
    TokenFixture *t = &token_test; CHECK(atomic_load(&t->server.accepted) > index);
    const char *request = t->server.requests[index]; const char *data = strstr(request, "\r\n\r\n"); CHECK(data != NULL); data += 4;
    const char *length = strstr(request, "Content-Length: "); CHECK(length != NULL);
    CHECK(strtoul(length + 16, NULL, 10) == bytes && memcmp(data, body, bytes) == 0);
    CHECK(strstr(request, login ? "POST /login5 HTTP/1.1" : "POST /clienttoken HTTP/1.1") != NULL);
    CHECK(strstr(request, "Content-Type: application/x-protobuf\r\n") != NULL && strstr(request, "Accept: application/x-protobuf\r\n") != NULL &&
        strstr(request, "Cache-Control: no-cache, no-store, max-age=0\r\n") != NULL);
    CHECK(strstr(request, "Authorization:") == NULL && strstr(request, "Cookie:") == NULL && strstr(request, "fixture-refresh") == NULL);
    CHECK((strstr(request, "client-token: fixture-client-token\r\n") != NULL) == login);
}
static void grant_and_scope(void)
{
    TestHttpResponse responses[] = {TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(login_ok)};
    token_test_open(responses, 2); TokenManager *m = token_test.manager; TokenAuthorization a = {0};
    CHECK(token_authorize(m, L"https://audio-ak-spotify-com.akamaized.net/file", &a) == TOKEN_SCOPE && !m->http_active);
    CHECK(token_ensure(m, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
    token_test_request(0, auth_client_request, sizeof(auth_client_request), false); token_test_request(1, auth_login_request, sizeof(auth_login_request), true);
    CHECK(token_info(m, TOKEN_CLIENT).expires_at == 121000 && token_info(m, TOKEN_CLIENT).refresh_at == 61000);
    CHECK(token_info(m, TOKEN_BEARER).expires_at == 121000 && token_info(m, TOKEN_BEARER).refresh_at == 91000);
    CHECK(token_next_wait_ms(m) == INFINITE && token_wait_handle(m) == NULL);
    const wchar_t *bad[] = {L"https://api-partner.spotify.com.evil/x", L"https://evilspotify.com/x", L"http://api-partner.spotify.com/x",
        L"https://api-partner.spotify.com@evil/x", L"https://api-partner.spotify.com:444/x", L"https://127.0.0.1/x", L"https://api-partner.spotify.com\\evil/x"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) { CHECK(token_authorize(m, bad[i], &a) == TOKEN_SCOPE && !a.issued); }
    CHECK(token_authorize(m, L"https://api-partner.spotify.com/pathfinder/v1/query", &a) == TOKEN_OK);
    CHECK(wcscmp(a.bearer, L"Bearer fixture-bearer") == 0 && wcscmp(a.client, L"fixture-client-token") == 0);
    CHECK(token_report_status(m, &a, 403) == TOKEN_FORBIDDEN && !m->http_active && token_info(m, TOKEN_BEARER).present);
    CHECK(token_test_zero(a.bearer, sizeof(a.bearer)) && token_test_zero(a.client, sizeof(a.client)));
    token_authorization_clear(&a); CHECK(token_test_zero(&a, sizeof(a))); token_test_close();
}
static void dedup_and_retry(void)
{
    TestHttpResponse responses[] = {TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(login_ok), TOKEN_RESPONSE_FIXTURE(login_ok)};
    responses[0].hold_body = true; token_test_open(responses, 3); TokenFixture *t = &token_test;
    CHECK(token_ensure(t->manager, TOKEN_BEARER) == TOKEN_PENDING && WaitForSingleObject(t->server.headers_sent, 3000) == WAIT_OBJECT_0);
    for (unsigned i = 0; i < 24; ++i) { CHECK(token_ensure(t->manager, TOKEN_BEARER) == TOKEN_PENDING); }
    CHECK(atomic_load(&t->server.accepted) == 1 && SetEvent(t->server.release_body)); CHECK(token_test_finish() == TOKEN_OK);
    CHECK(atomic_load(&t->server.accepted) == 2);
    TokenAuthorization first = {0}, late = {0};
    CHECK(token_authorize(t->manager, L"https://spclient.wg.spotify.com/storage-resolve/files/audio/interactive/fixture", &first) == TOKEN_OK);
    CHECK(token_authorize(t->manager, L"https://spclient.wg.spotify.com/metadata", &late) == TOKEN_OK);
    CHECK(token_report_status(t->manager, &first, 401) == TOKEN_RETRY);
    CHECK(token_authorize(t->manager, L"https://spclient.wg.spotify.com/metadata", &first) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
    CHECK(token_authorize(t->manager, L"https://spclient.wg.spotify.com/metadata", &first) == TOKEN_OK);
    uint64_t serial = token_info(t->manager, TOKEN_BEARER).serial;
    CHECK(token_report_status(t->manager, &late, 401) == TOKEN_RETRY && token_info(t->manager, TOKEN_BEARER).serial == serial);
    CHECK(token_authorize(t->manager, L"https://spclient.wg.spotify.com/metadata", &late) == TOKEN_OK && atomic_load(&t->server.accepted) == 3);
    CHECK(token_report_status(t->manager, &first, 401) == TOKEN_REJECTED && !t->manager->http_active);
    CHECK(token_authorize(t->manager, L"https://spclient.wg.spotify.com/metadata", &first) == TOKEN_REJECTED);
    CHECK(token_report_status(t->manager, &late, 200) == TOKEN_OK);
    token_authorization_clear(&first); token_authorization_clear(&late); token_test_close();
}
static void expiry_and_binding(void)
{
    TestHttpResponse responses[] = {TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(login_ok),
        TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(login_ok)};
    token_test_open(responses, 5); TokenFixture *t = &token_test;
    CHECK(token_ensure(t->manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
    TokenAuthorization late = {0}; CHECK(token_authorize(t->manager, L"https://dealer.spotify.com/", &late) == TOKEN_OK);
    uint64_t client = token_info(t->manager, TOKEN_CLIENT).serial, bearer = token_info(t->manager, TOKEN_BEARER).serial;
    t->now = 61000; CHECK(token_ensure(t->manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
    CHECK(token_info(t->manager, TOKEN_CLIENT).serial != client && token_info(t->manager, TOKEN_BEARER).serial == bearer);
    CHECK(token_report_status(t->manager, &late, 401) == TOKEN_RETRY && token_info(t->manager, TOKEN_BEARER).serial == bearer);
    token_authorization_clear(&late);
    t->now = 121000; CHECK(!token_info(t->manager, TOKEN_BEARER).present);
    CHECK(token_ensure(t->manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
    CHECK(token_info(t->manager, TOKEN_BEARER).serial != bearer && atomic_load(&t->server.accepted) == 5);
    t->now = 500; CHECK(token_ensure(t->manager, TOKEN_CLIENT) == TOKEN_CLOCK_CHANGED);
    CHECK(!token_info(t->manager, TOKEN_CLIENT).present && token_test_zero(t->manager->slots, sizeof(t->manager->slots)));
    CHECK(token_retry_refresh(t->manager) == TOKEN_OK);
    memcpy(t->record.client_id, "other-client", 13); CHECK(token_ensure(t->manager, TOKEN_CLIENT) == TOKEN_CLIENT_MISMATCH);
    token_test_close();
}
int main(void)
{
    grant_and_scope(); dedup_and_retry(); expiry_and_binding(); test_token_challenges(); test_token_errors();
    printf("Tokens: %zu arena bytes + %u validator scratch bytes; scoped refresh, hashcash and cleanup passed\n",
        sizeof(TokenManager), (unsigned)PROTO_MAX_BLOB); return 0;
}
