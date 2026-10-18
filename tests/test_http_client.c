#include "http_test.h"
#include <string.h>
#include <wchar.h>

Metrics http_test_metrics;
static _Alignas(HttpClient) unsigned char storage[MEMORY_BUDGET_HTTP];
static Arena arena;
static MetricsArena binding;

HttpClient *http_test_open(const wchar_t *origin)
{
    CHECK(arena_init(&arena, storage, sizeof(storage)));
    binding = (MetricsArena){0};
    CHECK(metrics_attach_arena(&http_test_metrics, &arena, &binding));
    HttpConfig config = {.allow_loopback_http = true, .metrics = &http_test_metrics};
    if (origin != NULL) { config.header_origins[0] = origin; config.header_origin_count = 1; }
    HttpClient *client;
    CHECK(http_client_create(&arena, &config, &client) == HTTP_OK);
    CHECK(arena.used == sizeof(HttpClient) && arena.used <= MEMORY_BUDGET_HTTP);
    CHECK(http_next_wait_ms(client) == INFINITE);
    CHECK(WaitForSingleObject(http_wait_handle(client), 0) == WAIT_TIMEOUT);
    return client;
}

void http_test_close(HttpClient *client)
{
    CHECK(http_client_shutdown(client, 5000) == HTTP_OK);
    CHECK(http_client_shutdown(client, 5000) == HTTP_CLOSED);
    arena_dispose(&arena);
}

void http_test_url(wchar_t *output, size_t chars, uint16_t port, const wchar_t *path)
{
    CHECK(swprintf_s(output, chars, L"http://127.0.0.1:%u%ls", (unsigned)port, path) > 0);
}

HttpRequest http_test_request(const wchar_t *url, void *body, size_t capacity)
{
    return (HttpRequest){.url = url, .method = HTTP_GET, .body = body,
        .body_capacity = capacity, .timeout_ms = 5000};
}

HttpCompletion http_test_complete(HttpClient *client, HttpRequestId id)
{
    HttpCompletion completion;
    uint64_t until = GetTickCount64() + 10000;
    while (http_consume(client, id, &completion) == HTTP_PENDING) {
        CHECK(GetTickCount64() < until);
        CHECK(http_wait(client, 1000) == HTTP_OK);
    }
    CHECK(completion.id.slot == id.slot && completion.id.generation == id.generation);
    return completion;
}

static void exchange_case(const TestHttpResponse *response, size_t capacity, HttpResult result, size_t expected)
{
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, response, 1));
    HttpClient *client = http_test_open(NULL);
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/fixture");
    unsigned char body[128]; memset(body, 0xa5, sizeof(body));
    HttpRequest request = http_test_request(url, body, capacity);
    HttpRequestId id;
    CHECK(http_submit(client, &request, &id) == HTTP_OK);
    HttpCompletion completion = http_test_complete(client, id);
    if (completion.result != result) {
        fprintf(stderr, "expected=%d actual=%d status=%u bytes=%zu\n", result, completion.result,
            completion.status, completion.bytes);
    }
    CHECK(completion.result == result && completion.final && completion.bytes == expected);
    if (completion.status != 0 && result != HTTP_HEADERS_TOO_LARGE) {
        CHECK(completion.headers != NULL && wcsstr(completion.headers, L"HTTP/1.1 ") == completion.headers);
    }
    CHECK(body[capacity] == 0xa5);
    if (expected != 0 && result == HTTP_OK) { CHECK(memcmp(body, "hello", expected) == 0); }
    CHECK(http_cancel(client, id) == HTTP_STALE);
    http_test_close(client); test_http_server_stop(&server);
    CHECK(metrics_balanced(&http_test_metrics));
}

static void response_cases(void)
{
    TestHttpResponse r = {"HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\n", "hello", 5, 1};
    exchange_case(&r, 5, HTTP_OK, 5);
    exchange_case(&r, 4, HTTP_BODY_TOO_LARGE, 0);
    r.headers = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";
    exchange_case(&r, 4, HTTP_BODY_TOO_LARGE, 0); /* oversized EOF-delimited body */
    r.headers = "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n";
    exchange_case(&r, 16, HTTP_TRUNCATED, 5);
    r = (TestHttpResponse){"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n",
        "2\r\nhe\r\n3\r\nllo\r\n0\r\n\r\n", 20, 1};
    exchange_case(&r, 5, HTTP_OK, 5);
    r = (TestHttpResponse){"HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
    exchange_case(&r, 16, HTTP_AUTH, 0);
    r.headers = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    exchange_case(&r, 16, HTTP_AUTH, 0);
    static char headers[HTTP_HEADER_LIMIT + 1024];
    const char *prefix = "HTTP/1.1 200 OK\r\nX-Large: ";
    memset(headers, 'x', sizeof(headers)); memcpy(headers, prefix, strlen(prefix));
    memcpy(headers + sizeof(headers) - 5, "\r\n\r\n", 5);
    r = (TestHttpResponse){.headers = headers};
    exchange_case(&r, 16, HTTP_HEADERS_TOO_LARGE, 0);
}

static void stream_blocks(void)
{
    const TestHttpResponse r = {.headers = "HTTP/1.1 206 Partial Content\r\nContent-Length: 131075\r\nConnection: close\r\n\r\n",
        .bytes = 131075, .fragment = 997, .fill = 0x37};
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, &r, 1));
    HttpClient *client = http_test_open(NULL);
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/stream");
    static unsigned char body[HTTP_BLOCK_SIZE + 1]; body[HTTP_BLOCK_SIZE] = 0x99;
    HttpRequest request = http_test_request(url, body, HTTP_BLOCK_SIZE);
    request.stream = true;
    HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
    size_t total = 0;
    for (size_t block = 0; block < 3; ++block) {
        /* Leave a full block unconsumed: owner does not schedule another read. */
        while (!client->slots[id.slot].ready) { CHECK(http_wait(client, 1000) == HTTP_OK); }
        if (block < 2) {
            CHECK(client->slots[id.slot].state == HS_BLOCK);
            CHECK(WaitForSingleObject(http_wait_handle(client), 0) == WAIT_TIMEOUT);
            CHECK(http_pump(client) == HTTP_OK && client->slots[id.slot].state == HS_BLOCK);
        }
        HttpCompletion completion = http_test_complete(client, id);
        CHECK(completion.result == HTTP_OK && completion.bytes == (block < 2 ? HTTP_BLOCK_SIZE : 3));
        CHECK(completion.final == (block == 2)); total += completion.bytes;
        CHECK(completion.total_bytes == total);
        for (size_t i = 0; i < completion.bytes; ++i) { CHECK(body[i] == 0x37); }
    }
    CHECK(total == r.bytes && body[HTTP_BLOCK_SIZE] == 0x99);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
}

static void retries(unsigned status)
{
    char headers[256];
    CHECK(sprintf_s(headers, sizeof(headers), "HTTP/1.1 %u Retry\r\nRetry-After: 0\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status) > 0);
    TestHttpResponse script[4];
    for (size_t i = 0; i < 4; ++i) { script[i] = (TestHttpResponse){.headers = headers}; }
    TestHttpServer server;
    CHECK(test_http_server_start(&server, &http_test_metrics, script, 4));
    HttpClient *client = http_test_open(NULL);
    wchar_t url[128]; http_test_url(url, 128, server.port, L"/retry");
    unsigned char body[16]; HttpRequest request = http_test_request(url, body, sizeof(body));
    request.max_retries = 3;
    HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
    HttpCompletion completion = http_test_complete(client, id);
    CHECK(completion.result == http_classify_status(status) && completion.attempts == 4);
    CHECK(atomic_load(&server.accepted) == 4);
    http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
}

static void redirects(void)
{
    TestHttpResponse target = {.headers = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
    TestHttpServer a, b;
    CHECK(test_http_server_start(&b, &http_test_metrics, &target, 1));
    char headers[256];
    CHECK(sprintf_s(headers, sizeof(headers), "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:%u/final\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", (unsigned)b.port) > 0);
    TestHttpResponse source = {.headers = headers};
    CHECK(test_http_server_start(&a, &http_test_metrics, &source, 1));
    wchar_t origin[128], url[128]; http_test_url(origin, 128, a.port, L"");
    http_test_url(url, 128, a.port, L"/redirect");
    HttpClient *client = http_test_open(origin);
    HttpHeader credentials[] = {{L"Authorization", L"Bearer fixture"}, {L"client-token", L"fixture-token"},
        {L"Cookie", L"fixture-cookie"}, {L"X-Secret", L"private"}};
    HttpRequest request = http_test_request(url, NULL, 0);
    request.headers = credentials; request.header_count = 4; request.max_redirects = 2;
    HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
    HttpCompletion completion = http_test_complete(client, id);
    CHECK(completion.result == HTTP_OK && completion.attempts == 2);
    CHECK(strstr(a.requests[0], "Bearer fixture") != NULL && strstr(a.requests[0], "fixture-cookie") != NULL);
    CHECK(strstr(b.requests[0], "Authorization:") == NULL && strstr(b.requests[0], "client-token:") == NULL);
    CHECK(strstr(b.requests[0], "Cookie:") == NULL && strstr(b.requests[0], "X-Secret:") == NULL);
    /* Direct headers to an origin absent from the allowlist are rejected. */
    http_test_url(url, 128, b.port, L"/forbidden");
    CHECK(http_submit(client, &request, &id) == HTTP_DESTINATION);
    http_test_close(client); test_http_server_stop(&a); test_http_server_stop(&b);
    CHECK(metrics_balanced(&http_test_metrics));
    TestHttpResponse cycle[3];
    for (size_t i = 0; i < 3; ++i) {
        cycle[i] = (TestHttpResponse){.headers = "HTTP/1.1 307 Again\r\nLocation: /loop\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
    }
    CHECK(test_http_server_start(&a, &http_test_metrics, cycle, 3));
    http_test_url(url, 128, a.port, L"/loop"); client = http_test_open(NULL);
    request = http_test_request(url, NULL, 0); request.max_redirects = 2;
    CHECK(http_submit(client, &request, &id) == HTTP_OK);
    completion = http_test_complete(client, id);
    CHECK(completion.result == HTTP_REDIRECT && completion.attempts == 3);
    http_test_close(client); test_http_server_stop(&a); CHECK(metrics_balanced(&http_test_metrics));
}

static void encodings(void)
{
    /* Independently generated RFC1952 gzip for five UTF-8 bytes, 'hello'. */
    static const unsigned char gzip[] = {31,139,8,0,0,0,0,0,2,3,203,72,205,201,201,7,0,134,166,16,54,5,0,0,0};
    TestHttpResponse response = {.headers = "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 25\r\nConnection: close\r\n\r\n",
        .body = gzip, .bytes = sizeof(gzip), .fragment = 2};
    exchange_case(&response, 16, HTTP_OK, 5);
    response.headers = "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nConnection: close\r\n\r\n";
    exchange_case(&response, 16, HTTP_OK, 5);
    response.headers = "HTTP/1.1 200 OK\r\nContent-Encoding: br\r\nContent-Length: 25\r\nConnection: close\r\n\r\n";
    exchange_case(&response, 16, HTTP_ENCODING, 0);
    /* .NET GZipStream fixture: 8192 'a' bytes in 44 compressed bytes. */
    static const unsigned char bomb[] = {31,139,8,0,0,0,0,0,4,0,237,193,1,13,0,0,0,194,160,172,239,95,
        194,28,110,64,1,0,0,0,0,0,0,0,239,6,213,102,111,13,0,32,0,0};
    response = (TestHttpResponse){.headers = "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 44\r\nConnection: close\r\n\r\n",
        .body = bomb, .bytes = sizeof(bomb)};
    for (unsigned ratio = 16; ratio <= 1024; ratio *= 64) {
        TestHttpServer server;
        CHECK(test_http_server_start(&server, &http_test_metrics, &response, 1));
        HttpClient *client = http_test_open(NULL);
        wchar_t url[128]; http_test_url(url, 128, server.port, L"/expansion");
        static unsigned char body[8193]; body[8192] = 0x91;
        HttpRequest request = http_test_request(url, body, 8192); request.max_expansion = ratio;
        HttpRequestId id; CHECK(http_submit(client, &request, &id) == HTTP_OK);
        HttpCompletion completion = http_test_complete(client, id);
        CHECK(completion.result == (ratio == 16 ? HTTP_BODY_TOO_LARGE : HTTP_OK));
        CHECK(body[8192] == 0x91);
        if (ratio == 1024) { CHECK(completion.bytes == 8192 && body[0] == 'a' && body[8191] == 'a'); }
        http_test_close(client); test_http_server_stop(&server); CHECK(metrics_balanced(&http_test_metrics));
    }
}

int main(void)
{
    CHECK(metrics_init(&http_test_metrics));
    CHECK(http_retry_delay_ms(0, L"0", 0) == 250);
    CHECK(http_retry_delay_ms(1, L"2", 0) == 2000);
    CHECK(http_retry_delay_ms(2, L"999999999999999999", 0) == 30000);
    CHECK(http_retry_delay_ms(3, L"nonsense", 0) == 2000);
    CHECK(http_retry_delay_ms(0, L"Thu, 01 Jan 1970 00:00:02 GMT", 11644473600000ULL) == 2000);
    response_cases(); stream_blocks(); retries(429); retries(503); redirects(); encodings();
    test_http_lifetime(); test_http_policy();
    CHECK(metrics_balanced(&http_test_metrics));
    puts("WinHTTP bounded bodies/headers, blocks, redirects, retries and callback lifetimes passed");
    return 0;
}
