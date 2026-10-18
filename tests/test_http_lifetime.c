#include "http_test.h"
#include <string.h>

static const char held_headers[] = "HTTP/1.1 200 OK\r\nContent-Length: 65536\r\nConnection: close\r\n\r\n";

static void reach(HttpClient *client, HttpRequestId id, HttpState state)
{
    uint64_t until = GetTickCount64() + 5000;
    while (client->slots[id.slot].state != state) {
        if (GetTickCount64() >= until) { fprintf(stderr, "wanted phase=%d observed=%d\n", state, client->slots[id.slot].state); }
        CHECK(GetTickCount64() < until);
        CHECK(http_wait(client, 1000) == HTTP_OK);
        CHECK(client->slots[id.slot].state != HS_DONE);
    }
}

static void cancel_phase(HttpState phase, bool shutdown)
{
    TestHttpResponse response = {.headers = held_headers, .bytes = HTTP_BLOCK_SIZE,
        .hold_body = phase == HS_HEADERS || phase == HS_READ, .fill = 0x71};
    if (phase == HS_CLOSING) {
        response = (TestHttpResponse){.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
    }
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, &response, 1));
    HttpClient *client = http_test_open(NULL);
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/cancel");
    static unsigned char body[HTTP_BLOCK_SIZE];
    HttpRequest request = http_test_request(url, body, sizeof(body)); request.stream = true;
    HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
    reach(client, id, phase);
    if (shutdown) {
        /* A zero-time shutdown cannot reclaim a still-live request, even if
         * HANDLE_CLOSING has already reached the queue. Retry is supported. */
        CHECK(http_client_shutdown(client, 0) == HTTP_TIMEOUT);
        CHECK(http_submit(client, &request, &id) == HTTP_CLOSED);
    } else {
        CHECK(http_cancel(client, id) == HTTP_OK);
        CHECK(http_cancel(client, id) == HTTP_OK); /* idempotent close initiation */
        HttpCompletion completion;
        CHECK(http_consume(client, id, &completion) == HTTP_PENDING);
        CHECK(client->slots[id.slot].state == HS_CLOSING);
        completion = http_test_complete(client, id);
        CHECK(completion.result == HTTP_CANCELLED && completion.final);
    }
    http_test_close(client); test_http_server_stop(&server);
    CHECK(metrics_balanced(&http_test_metrics));
}

static void fixed_slots_and_shutdown(void)
{
    const TestHttpResponse response = {.headers = held_headers, .hold_headers = true};
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, &response, 1));
    HttpClient *client = http_test_open(NULL);
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/pool");
    HttpRequest request = http_test_request(url, NULL, 0);
    HttpRequestId ids[HTTP_SLOT_COUNT], extra;
    CHECK(HTTP_SLOT_COUNT == 8);
    for (uint32_t i = 0; i < HTTP_SLOT_COUNT; ++i) {
        CHECK(http_submit(client, &request, &ids[i]) == HTTP_OK && ids[i].slot == i);
    }
    CHECK(http_submit(client, &request, &extra) == HTTP_FULL);
    for (uint32_t i = 0; i < HTTP_SLOT_COUNT; ++i) { CHECK(http_cancel(client, ids[i]) == HTTP_OK); }
    /* Closing handles have not freed admission. */
    CHECK(http_submit(client, &request, &extra) == HTTP_FULL);
    for (uint32_t i = 0; i < HTTP_SLOT_COUNT; ++i) {
        HttpCompletion completion = http_test_complete(client, ids[i]);
        CHECK(completion.result == HTTP_CANCELLED && completion.final);
    }
    CHECK(client->count == 0 && !client->overflow && http_next_wait_ms(client) == INFINITE);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
}

static void prepend(HttpClient *client, HttpCallback record)
{
    AcquireSRWLockExclusive(&client->lock);
    CHECK(client->count < HTTP_CALLBACK_CAPACITY);
    client->head = (client->head + HTTP_CALLBACK_CAPACITY - 1) % HTTP_CALLBACK_CAPACITY;
    client->queue[client->head] = record; ++client->count;
    CHECK(SetEvent(client->wake));
    ReleaseSRWLockExclusive(&client->lock);
}

static void stale_records(void)
{
    const TestHttpResponse script[2] = {
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers = held_headers, .hold_headers = true}};
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, script, 2));
    HttpClient *client = http_test_open(NULL);
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/generation");
    HttpRequest request = http_test_request(url, NULL, 0);
    HttpRequestId old, current;
    CHECK(http_submit(client, &request, &old) == HTTP_OK);
    CHECK(http_test_complete(client, old).result == HTTP_OK);
    CHECK(http_submit(client, &request, &current) == HTTP_OK);
    CHECK(current.slot == old.slot && current.generation == old.generation + 1);
    HttpSlot *slot = &client->slots[current.slot];
    prepend(client, (HttpCallback){old, slot->attempt, WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, 0, ERROR_INVALID_DATA});
    CHECK(http_pump(client) == HTTP_OK && slot->state == HS_SEND);
    prepend(client, (HttpCallback){current, slot->attempt + 1, WINHTTP_CALLBACK_STATUS_REQUEST_ERROR, 0, ERROR_INVALID_DATA});
    CHECK(http_pump(client) == HTTP_OK && slot->state == HS_SEND);
    CHECK(http_cancel(client, old) == HTTP_STALE);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
}

static uint64_t fake_now(void *context) { return *(const uint64_t *)context; }

static void deadlines_and_backoff(void)
{
    TestHttpResponse response = {.headers = held_headers, .hold_body = true};
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, &response, 1));
    HttpClient *client = http_test_open(NULL);
    uint64_t now = 1000;
    client->config.clock = (CoreClock){fake_now, &now};
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/timeout");
    HttpRequest request = http_test_request(url, NULL, 0);
    HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
    reach(client, id, HS_HEADERS);
    CHECK(http_next_wait_ms(client) == 5000);
    now += 5000; CHECK(http_pump(client) == HTTP_OK);
    CHECK(http_test_complete(client, id).result == HTTP_TIMEOUT);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
    response = (TestHttpResponse){.headers = "HTTP/1.1 429 Retry\r\nRetry-After: 30\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
    CHECK(test_http_server_start(&server, &http_test_metrics, &response, 1));
    client = http_test_open(NULL); http_test_url(url, 128, server.port, L"/backoff");
    request = http_test_request(url, NULL, 0); request.max_retries = 3;
    CHECK(http_submit(client, &request, &id) == HTTP_OK);
    reach(client, id, HS_BACKOFF);
    CHECK(http_next_wait_ms(client) > 0 && http_next_wait_ms(client) <= 5000);
    CHECK(http_cancel(client, id) == HTTP_OK);
    CHECK(http_test_complete(client, id).result == HTTP_CANCELLED);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
}

static void invalid_and_budget(void)
{
    unsigned char tiny_storage[64];
    Arena scratch;
    CHECK(arena_init(&scratch, tiny_storage, sizeof(tiny_storage)));
    HttpConfig config = {0}; HttpClient *client = NULL;
    CHECK(http_client_create(&scratch, &config, &client) == HTTP_NO_MEMORY && client == NULL && scratch.used == 0);
    arena_dispose(&scratch);
    client = http_test_open(NULL);
    HttpRequest request = http_test_request(L"https://example.invalid/", NULL, 0);
    HttpRequestId id;
    request.headers = (HttpHeader[]){{L"Authorization", L"fixture"}}; request.header_count = 1;
    CHECK(http_submit(client, &request, &id) == HTTP_DESTINATION);
    request.header_count = 0;
    request.url = L"http://example.invalid/";
    CHECK(http_submit(client, &request, &id) == HTTP_DESTINATION);
    request.url = L"https://user:password@example.invalid/";
    CHECK(http_submit(client, &request, &id) == HTTP_INVALID);
    request.url = L"https://example.invalid/#fragment";
    CHECK(http_submit(client, &request, &id) == HTTP_INVALID);
    request.url = L"https://example.invalid/"; request.stream = true;
    CHECK(http_submit(client, &request, &id) == HTTP_INVALID);
    request.stream = false; request.max_retries = 4;
    CHECK(http_submit(client, &request, &id) == HTTP_INVALID);
    http_test_close(client); CHECK(metrics_balanced(&http_test_metrics));
}

void test_http_lifetime(void)
{
    const HttpState phases[] = {HS_SEND, HS_HEADERS, HS_READ, HS_BLOCK, HS_CLOSING};
    for (size_t i = 0; i < sizeof(phases) / sizeof(phases[0]); ++i) {
        cancel_phase(phases[i], false); cancel_phase(phases[i], true);
    }
    fixed_slots_and_shutdown(); stale_records(); deadlines_and_backoff(); invalid_and_budget();
}
