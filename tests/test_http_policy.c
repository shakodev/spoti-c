#include "http_test.h"
#include <string.h>
#include <wchar.h>

static void host_pool(void)
{
    static TestHttpServer servers[HTTP_HOST_COUNT + 1];
    const TestHttpResponse responses[2] = {
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"}};
    for (size_t i = 0; i <= HTTP_HOST_COUNT; ++i) {
        CHECK(test_http_server_start(&servers[i], &http_test_metrics, responses, i == 0 ? 2 : 1));
    }
    HttpClient *client = http_test_open(NULL);
    for (size_t i = 0; i <= HTTP_HOST_COUNT; ++i) {
        wchar_t url[128]; http_test_url(url, 128, servers[i].port, L"/hosts");
        HttpRequest request = http_test_request(url, NULL, 0);
        HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
        CHECK(http_test_complete(client, id).result == HTTP_OK);
        size_t hosts = 0;
        for (size_t h = 0; h < HTTP_HOST_COUNT; ++h) {
            CHECK(client->hosts[h].users == 0);
            if (client->hosts[h].handle != NULL) { ++hosts; }
        }
        CHECK(hosts == (i < HTTP_HOST_COUNT ? i + 1 : HTTP_HOST_COUNT));
        if (i == 0) {
            HINTERNET original = client->hosts[0].handle;
            CHECK(http_submit(client, &request, &id) == HTTP_OK);
            CHECK(http_test_complete(client, id).result == HTTP_OK);
            CHECK(client->hosts[0].handle == original); /* retained logical connection */
        }
    }
    http_test_close(client);
    for (size_t i = 0; i <= HTTP_HOST_COUNT; ++i) { test_http_server_stop(&servers[i]); }
    CHECK(metrics_balanced(&http_test_metrics));
}

static void safe_replays(void)
{
    const TestHttpResponse recovery[3] = {
        {.headers = "HTTP/1.1 429 Retry\r\nRetry-After: 0\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers = "HTTP/1.1 503 Retry\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n", .body = "hello", .bytes = 5}};
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, recovery, 3));
    HttpClient *client = http_test_open(NULL);
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/recover");
    char body[5]; HttpRequest request = http_test_request(url, body, sizeof(body)); request.max_retries = 3;
    HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
    HttpCompletion completion = http_test_complete(client, id);
    CHECK(completion.result == HTTP_OK && completion.attempts == 3 && completion.bytes == 5);
    CHECK(memcmp(body, "hello", 5) == 0 && atomic_load(&server.accepted) == 3);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
    const TestHttpResponse cases[] = {
        {.headers = "HTTP/1.1 503 Retry\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers = "HTTP/1.1 302 Found\r\nLocation: /next\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"},
        {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 2000000\r\nConnection: close\r\n\r\n"}};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(test_http_server_start(&server, &http_test_metrics, &cases[i], 1));
        client = http_test_open(NULL); http_test_url(url, 128, server.port, L"/no-replay");
        request = http_test_request(url, NULL, 0);
        request.method = i < 2 ? HTTP_POST : HTTP_HEAD;
        request.upload = i < 2 ? "fixture upload" : NULL;
        request.upload_bytes = i < 2 ? 14 : 0;
        request.max_retries = 3; request.max_redirects = 5;
        CHECK(http_submit(client, &request, &id) == HTTP_OK);
        completion = http_test_complete(client, id);
        CHECK(completion.attempts == 1 && completion.final && completion.bytes == 0);
        CHECK(completion.result == (i == 0 ? HTTP_SERVER_ERROR : i == 1 ? HTTP_REDIRECT : HTTP_OK));
        CHECK(atomic_load(&server.accepted) == 1);
        http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
    }
}

static void header_validation(void)
{
    const TestHttpResponse response = {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, &response, 1));
    wchar_t url[128], origin[128]; http_test_url(url, 128, server.port, L"/headers");
    http_test_url(origin, 128, server.port, L"");
    HttpClient *client = http_test_open(origin);
    const HttpHeader headers[] = {{L"X-Secret", L"ok\r\nInjected: true"}, {L"Host", L"evil.invalid"},
        {L"Accept-Encoding", L"br"}, {L"Bad Name", L"value"}, {L"Content-Length", L"0"}};
    for (size_t i = 0; i < sizeof(headers) / sizeof(headers[0]); ++i) {
        HttpRequest request = http_test_request(url, NULL, 0);
        request.headers = &headers[i]; request.header_count = 1;
        HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_INVALID && id.generation == 0);
    }
    CHECK(atomic_load(&server.accepted) == 0);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
}

void test_http_policy(void)
{
    host_pool(); safe_replays(); header_validation();
}
