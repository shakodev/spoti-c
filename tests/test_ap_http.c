#include "ap_session_test.h"
#include "support/http_server.h"
#include <wchar.h>

static _Alignas(16) uint8_t http_memory[MEMORY_BUDGET_HTTP];

static void drive_http(void)
{
    ApSession *s = ap_fixture.session;
    HANDLE handles[3]; size_t count = ap_wait_handles(s, handles, 3);
    CHECK(count != 0 && WaitForMultipleObjects((DWORD)count, handles, FALSE, 5000) == WAIT_OBJECT_0);
    (void)ap_fixture_event(AP_EVENT_HTTP);
}

static void http_case(const char *body, ApResult expected, bool cancel)
{
    ap_fixture_open(); ApTestFixture *t = &ap_fixture;
    Arena http_arena; MetricsArena binding = {0};
    CHECK(arena_init(&http_arena, http_memory, sizeof(http_memory)));
    CHECK(metrics_attach_arena(&t->metrics, &http_arena, &binding));
    HttpConfig config = {.allow_loopback_http = true, .metrics = &t->metrics};
    HttpClient *http; CHECK(http_client_create(&http_arena, &config, &http) == HTTP_OK);
    char headers[160];
    CHECK(sprintf_s(headers, sizeof(headers), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(body)) > 0);
    TestHttpResponse response = {.headers = headers, .body = body, .bytes = strlen(body), .hold_headers = cancel};
    TestHttpServer server; CHECK(test_http_server_start(&server, &t->metrics, &response, 1));
    wchar_t url[128]; CHECK(swprintf_s(url, 128, L"http://127.0.0.1:%u/resolve", (unsigned)server.port) > 0);
    t->session->config.http = http; t->session->config.resolver_url = url;
    CHECK(ap_start(t->session, NULL) == AP_OK && ap_state(t->session) == AP_STATE_RESOLVE);
    if (cancel) {
        CHECK(WaitForSingleObject(server.seen, 5000) == WAIT_OBJECT_0);
        CHECK(ap_fixture_event(AP_EVENT_CANCEL) == AP_CANCELLED && t->session->http_active);
        CHECK(ap_start(t->session, &t->endpoints) == AP_BAD_STATE);
        while (t->session->http_active) { drive_http(); }
        CHECK(ap_state(t->session) == AP_STATE_CLOSED && ap_result(t->session) == AP_CANCELLED);
    } else {
        while (ap_state(t->session) == AP_STATE_RESOLVE) { drive_http(); }
        if (expected == AP_OK) {
            CHECK(ap_state(t->session) == AP_STATE_CONNECT && t->fake.begins == 1);
            CHECK(strcmp(t->fake.chosen[0].host, "fake-a.invalid") == 0 && t->fake.chosen[0].port == 4070);
        } else { CHECK(ap_state(t->session) == AP_STATE_CLOSED && ap_result(t->session) == expected); }
    }
    CHECK(strstr(server.requests[0], "GET /resolve HTTP/1.1") != NULL);
    CHECK(strstr(server.requests[0], "Authorization:") == NULL && strstr(server.requests[0], "client-token:") == NULL);
    CHECK(http_client_shutdown(http, 5000) == HTTP_OK); arena_dispose(&http_arena);
    test_http_server_stop(&server); ap_fixture_close();
}

void test_ap_http(void)
{
    static const char valid[] = "{\"accesspoint\":[\"fake-a.invalid:4070\"],\"dealer\":[\"dealer:443\"],\"spclient\":[\"spclient:443\"]}";
    http_case(valid, AP_OK, false);
    http_case("{\"accesspoint\":null}", AP_MALFORMED, false);
    http_case(valid, AP_CANCELLED, true);
}
