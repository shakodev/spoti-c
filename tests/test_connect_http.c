#include "support/http_server.h"
#include "player_test.h"
#include "protocol/connect_internal.h"

static _Alignas(16) unsigned char http_memory[MEMORY_BUDGET_HTTP], state_memory[65536];
void connect_http_tests(void)
{
    TestHttpResponse responses[] = {
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", .capture_upload = true, .hold_headers = true},
        {.headers = "HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", .capture_upload = true},
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", .capture_upload = true},
        {.headers = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", .capture_upload = true},
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", .capture_upload = true, .hold_headers = true}};
    Metrics metrics; CHECK(metrics_init(&metrics));
    TestHttpServer server; CHECK(test_http_server_start(&server, &metrics, responses, 5));
    wchar_t origin[128]; CHECK(swprintf_s(origin, 128, L"http://127.0.0.1:%u", server.port) > 0);
    Arena http_arena, core; CHECK(arena_init(&http_arena, http_memory, sizeof(http_memory)));
    CHECK(arena_init(&core, state_memory, sizeof(state_memory))); pt_open();
    HttpConfig hc = {.allow_loopback_http = true, .header_origin_count = 1, .metrics = &metrics}; hc.header_origins[0] = origin;
    HttpClient *http; CHECK(http_client_create(&http_arena, &hc, &http) == HTTP_OK);
    ConnectConfig cfg = {.player = pt.player, .http = http, .fixture_http_origin = origin,
        .device_id = "fixture-device", .name = "Fixture", .client_id = "fixture-client"};
    ConnectState *state; CHECK(connect_create(&core, &cfg, &state) == DEALER_OK);
    CHECK(connect_connection(state, "old-connection") == DEALER_OK && connect_poll(state) == DEALER_PENDING);
    CHECK(WaitForSingleObject(server.seen, 5000) == WAIT_OBJECT_0);
    CHECK(connect_connection(state, "new-connection") == DEALER_OK);
    SetEvent(server.release_headers);
    uint64_t until = GetTickCount64() + 10000;
    while (!connect_snapshot(state).registered) {
        CHECK(GetTickCount64() < until);
        DealerResult r = connect_poll(state); CHECK(r == DEALER_OK || r == DEALER_PENDING);
        if (atomic_load(&server.accepted) < 3) { CHECK(!connect_snapshot(state).registered); }
        (void)WaitForSingleObject(http_wait_handle(http), 10);
    }
    CHECK(strstr(server.requests[0], "PUT /connect-state/v1/devices/fixture-device HTTP/1.1"));
    CHECK(strstr(server.requests[0], "X-Spotify-Connection-Id: old-connection"));
    CHECK(strstr(server.requests[1], "X-Spotify-Connection-Id: new-connection"));
    CHECK(strstr(server.requests[2], "Content-Type: application/x-protobuf"));
    CHECK(strstr(server.requests[2], "Authorization: Bearer fixture"));
    connect_changed(state); DealerResult r = connect_poll(state);
    while (r == DEALER_PENDING) { CHECK(GetTickCount64() < until); (void)http_wait(http, 10); r = connect_poll(state); }
    CHECK(r == DEALER_AUTH && atomic_load(&server.accepted) == 4);
    ResetEvent(server.release_headers);
    CHECK(connect_connection(state, "cancelled-connection") == DEALER_OK && connect_poll(state) == DEALER_PENDING);
    while (atomic_load(&server.accepted) < 5) { CHECK(GetTickCount64() < until); (void)http_wait(http, 10); }
    CHECK(state->pending);
    while (connect_shutdown(state) == DEALER_PENDING) { CHECK(GetTickCount64() < until); (void)http_wait(http, 10); }
    CHECK(!state->pending);
    CHECK(http_client_shutdown(http, 5000) == HTTP_OK); test_http_server_stop(&server);
    CHECK(metrics_balanced(&metrics));
    arena_dispose(&http_arena); arena_dispose(&core); pt_close();
}
