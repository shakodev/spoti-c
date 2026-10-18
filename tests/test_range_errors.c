#include "range_test.h"

static void parse_cases(void)
{
    const wchar_t *bad[] = {
        L"Content-Range: bytes 1-65535/131072\r\n", L"Content-Range: bytes 0-65536/131072\r\n",
        L"Content-Range: bytes 0-65534/131072\r\n", L"Content-Range: bytes 65535-0/131072\r\n",
        L"Content-Range: bytes 0-65535/65535\r\n", L"Content-Range: bytes 0-65535/*\r\n",
        L"Content-Range: bytes 0-65535/18446744073709551616\r\n", L"Content-Range: bits 0-65535/131072\r\n",
        L"Content-Range: bytes 0-65535/131072\r\nContent-Range: bytes 0-65535/131072\r\n",
        L"Content-Range: bytes 0-65535/131072\r\nContent-Encoding: gzip\r\n",
        L"Content-Range: bytes 0-65535/131072\r\nContent-Type: multipart/byteranges; boundary=x\r\n",
        L"Content-Range: bytes 0-65535/131072\r\nETag: W/\"weak\"\r\n", L"Content-Length: 65536\r\n",
        L"Content-Range: bytes 0-65535/131072\r\nETag: unquoted\r\n",
        L"Content-Range: bytes 0-65535/131072\r\nETag: \"has space\"\r\n"
    };
    HttpCompletion c = {.result = HTTP_OK, .status = 206, .bytes = RANGE_BLOCK_BYTES};
    uint64_t total; size_t bytes; wchar_t etag[128];
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        c.headers = bad[i]; CHECK(range_parse_response(&c, 0, 65535, &total, &bytes, etag) == RANGE_HEADER);
    }
    c.headers = L"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-65535/131072\r\nETag: \"strong\"\r\n\r\n";
    CHECK(range_parse_response(&c, 0, 65535, &total, &bytes, etag) == RANGE_OK && total == 131072 && bytes == 65536);
    c.bytes = 2; CHECK(range_parse_response(&c, 0, 65535, &total, &bytes, etag) == RANGE_TRUNCATED);
    c.status = 416; c.headers = L"Content-Range: bytes */65536\r\n";
    CHECK(range_parse_response(&c, 65536, 131071, &total, &bytes, etag) == RANGE_EOF && total == 65536 && bytes == 0);
    CHECK(range_parse_response(&c, 0, 65535, &total, &bytes, etag) == RANGE_HEADER);
}
static void http_errors(void)
{
    struct { const char *headers; size_t bytes; RangeResult expected; uint64_t offset; } cases[] = {
        {"HTTP/1.1 200 OK\r\nContent-Length: 16\r\nConnection: close\r\n\r\n", 16, RANGE_UNSUPPORTED, 0},
        {"HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */65536\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 0, RANGE_EOF, 65536},
        {"HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */65536\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 0, RANGE_HEADER, 0},
        {"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-65535/65536\r\nContent-Length: 65536\r\nConnection: close\r\n\r\n", 16, RANGE_TRUNCATED, 0},
        {"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 1-16/65536\r\nContent-Length: 16\r\nConnection: close\r\n\r\n", 16, RANGE_HEADER, 0},
        {"HTTP/1.1 206 Partial Content\r\nContent-Range: bytes 0-65535/65536\r\nContent-Encoding: gzip\r\nContent-Length: 16\r\nConnection: close\r\n\r\n", 16, RANGE_HEADER, 0},
        {"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 0, RANGE_HTTP_ERROR, 0}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        RangePlan plan = {0}; plan.count = 1; plan.replies[0] = (TestHttpResponse){.headers = cases[i].headers, .bytes = cases[i].bytes};
        rt_open(&plan, true, 0, 0, 0); RangeId id = rt_request(cases[i].offset, 1, RANGE_CURRENT);
        CHECK(rt_finish(id) == cases[i].expected && range_stats(rt.fetcher).network_attempts == 1);
        rt_cache_drain(); CHECK(audio_cache_stats(rt.cache).entries == 0); rt_close();
    }
}
static void refresh_and_retry(void)
{
    RangePlan plan = {0}; rt_plan(&plan, 0, 0, RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x44);
    rt_open(&plan, false, 0, 0, 0); rt.fetcher->sources[rt.source.slot].urls[0].refresh_at = 0; rt.refresh_pending = true;
    RangeId id = rt_request(0, 1, RANGE_CURRENT); CHECK(range_poll(rt.fetcher) == RANGE_OK);
    CHECK(rt.refreshes == 1 && range_stats(rt.fetcher).network_attempts == 0 && range_next_wait_ms(rt.fetcher) > 0);
    rt.refresh_pending = false; CHECK(rt_finish(id) == RANGE_OK && rt.refreshes == 2); rt_close();
    memset(&plan, 0, sizeof(plan));
    plan.replies[0].headers = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    rt_plan(&plan, 1, 0, RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x44);
    rt_open(&plan, false, 0, 0, 0); id = rt_request(0, 1, RANGE_CURRENT);
    CHECK(rt_finish(id) == RANGE_OK && rt.refreshes == 1 && range_stats(rt.fetcher).network_attempts == 2); rt_close();
    rt_open(NULL, false, 0, 0, 0); rt.fetcher->sources[rt.source.slot].urls[0].refresh_at = 0; rt.refresh_changed = true;
    id = rt_request(0, 1, RANGE_CURRENT); CHECK(rt_finish(id) == RANGE_CHANGED && range_stats(rt.fetcher).network_attempts == 0); rt_close();
    memset(&plan, 0, sizeof(plan));
    plan.replies[0].headers = "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    rt_plan(&plan, 1, 0, RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x44);
    rt_open(&plan, false, 0, 0, 0);
    RangeSource source; rt.media.url_count = 2; rt.media.urls[1] = rt.media.urls[0];
    CHECK(strcat_s(rt.media.urls[1].url, MEDIA_URL_BYTES, "-fallback") == 0);
    CHECK(range_source_open(rt.fetcher, &rt.media, &source) == RANGE_OK); rt.source = source;
    id = rt_request(0, 1, RANGE_CURRENT); CHECK(rt_finish(id) == RANGE_OK && range_stats(rt.fetcher).network_attempts == 2);
    CHECK(strstr(rt.server.requests[1], "GET /encrypted-fallback ") != NULL); rt_close();
    memset(&plan, 0, sizeof(plan)); plan.count = 3;
    for (size_t i = 0; i < 3; ++i) { plan.replies[i].headers = "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"; }
    rt_open(&plan, false, 0, 0, 0); id = rt_request(0, 1, RANGE_CURRENT);
    CHECK(rt_finish(id) == RANGE_HTTP_ERROR && range_stats(rt.fetcher).network_attempts == 3); rt_close();
}
static void generation_and_representation(void)
{
    RangePlan plan = {0}; rt_plan(&plan, 0, 0, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x11); plan.replies[0].hold_body = true;
    rt_plan(&plan, 1, 0, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x22); rt_open(&plan, false, 0, 0, 0);
    RangeSource old = rt.source; RangeId cancelled = rt_request(0, 1, RANGE_CURRENT); CHECK(range_poll(rt.fetcher) == RANGE_OK);
    CHECK(WaitForSingleObject(rt.server.headers_sent, 5000) == WAIT_OBJECT_0);
    CHECK(range_cancel_generation(rt.fetcher, old) == RANGE_OK && range_status(rt.fetcher, cancelled, false) == RANGE_CANCELLED);
    CHECK(range_source_open(rt.fetcher, &rt.media, &rt.source) == RANGE_OK && rt.source.generation != old.generation);
    RangeId fresh = rt_request(0, 1, RANGE_CURRENT); CHECK(SetEvent(rt.server.release_body)); CHECK(rt_finish(fresh) == RANGE_OK);
    RangeLease lease = {0}; CHECK(range_acquire(rt.fetcher, rt.source, 0, 1, &lease) == RANGE_OK); rt_check_lease(&lease, 0x22);
    CHECK(range_status(rt.fetcher, fresh, true) == RANGE_OK && range_cancel_generation(rt.fetcher, rt.source) == RANGE_OK);
    CHECK(range_acquire(rt.fetcher, old, 0, 1, &(RangeLease){0}) == RANGE_STALE);
    CHECK(range_fetcher_shutdown(rt.fetcher) == RANGE_PENDING); rt_check_lease(&lease, 0x22);
    CHECK(range_release(rt.fetcher, &lease) == RANGE_OK); rt_close();
    memset(&plan, 0, sizeof(plan)); rt_plan(&plan, 0, 0, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x11);
    rt_plan(&plan, 1, RANGE_BLOCK_BYTES, 2 * RANGE_BLOCK_BYTES, RANGE_BLOCK_BYTES, 0x22);
    char *tag = strstr(plan.headers[1], "encrypted-fixture"); CHECK(tag != NULL); tag[0] = 'X';
    rt_open(&plan, false, 0, 0, 0); RangeId first = rt_request(0, 1, RANGE_CURRENT); CHECK(rt_finish(first) == RANGE_OK);
    CHECK(range_acquire(rt.fetcher, rt.source, 0, 1, &lease) == RANGE_OK && range_status(rt.fetcher, first, true) == RANGE_OK);
    RangeId second = rt_request(RANGE_BLOCK_BYTES, 1, RANGE_CURRENT); CHECK(rt_finish(second) == RANGE_CHANGED);
    rt_check_lease(&lease, 0x11); CHECK(range_release(rt.fetcher, &lease) == RANGE_OK); rt_close();
}
void range_error_tests(void) { parse_cases(); http_errors(); refresh_and_retry(); generation_and_representation(); }
