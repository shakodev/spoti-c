#ifndef SPOTIC_API_HTTP_INTERNAL_H
#define SPOTIC_API_HTTP_INTERNAL_H
#include "http_client.h"
#include "core/memory_budget.h"
#include <winhttp.h>

#define HTTP_CALLBACK_CAPACITY 32u
#define HTTP_READ_SIZE 8192u
#define HTTP_REQUEST_HEADERS 2048u
typedef enum HttpState { HS_FREE, HS_SEND, HS_HEADERS, HS_READ, HS_BLOCK,
    HS_CLOSING, HS_BACKOFF, HS_DONE } HttpState;
typedef enum HttpAfterClose { HA_FINISH, HA_RETRY, HA_REDIRECT } HttpAfterClose;
typedef struct HttpOrigin {
    wchar_t host[256];
    INTERNET_PORT port;
    bool secure;
} HttpOrigin;
typedef struct HttpHost {
    HttpOrigin origin;
    HINTERNET handle;
    MetricsHandle metric;
    unsigned users;
} HttpHost;
typedef struct HttpCallback {
    HttpRequestId id;
    uint32_t attempt;
    DWORD status, bytes, error;
} HttpCallback;
typedef struct HttpSlot {
    HttpClient *client;
    uint32_t generation, attempt;
    HttpState state;
    HttpAfterClose after_close;
    HINTERNET handle;
    MetricsHandle metric;
    HttpHost *host;
    HttpRequest request;
    wchar_t url[HTTP_URL_CHARS], headers[HTTP_REQUEST_HEADERS];
    wchar_t response_headers[HTTP_HEADER_LIMIT / sizeof(wchar_t)];
    unsigned char read_buffer[HTTP_READ_SIZE];
    HttpResult result;
    unsigned status, redirects, retries;
    uint64_t deadline, retry_at;
    size_t bytes, total, decoded_limit, wire_length;
    bool has_length, encoded, delivered, stripped, ready;
} HttpSlot;
struct HttpClient {
    HttpConfig config;
    HttpOrigin allowed[HTTP_HOST_COUNT];
    HINTERNET session;
    HANDLE wake;
    MetricsHandle session_metric, wake_metric;
    SRWLOCK lock;
    HttpCallback queue[HTTP_CALLBACK_CAPACITY];
    size_t head, count;
    bool overflow, stopping;
    HttpHost hosts[HTTP_HOST_COUNT];
    HttpSlot slots[HTTP_SLOT_COUNT];
};
_Static_assert(sizeof(HttpClient) < MEMORY_BUDGET_HTTP, "HTTP fixed storage budget");

bool http_origin_parse(const wchar_t *url, HttpOrigin *origin, wchar_t *path, size_t capacity);
bool http_origin_equal(const HttpOrigin *a, const HttpOrigin *b);
bool http_destination_allowed(const HttpClient *client, const HttpOrigin *origin);
bool http_origin_valid(const HttpClient *client, const HttpOrigin *origin);
HttpResult http_host_acquire(HttpClient *client, const HttpOrigin *origin, HttpHost **output);
void http_host_release(HttpSlot *slot);
void http_callback_enqueue(HttpSlot *slot, DWORD status, DWORD bytes, DWORD error);
bool http_callback_take(HttpClient *client, HttpCallback *output);
void CALLBACK http_status_callback(HINTERNET handle, DWORD_PTR context, DWORD status,
    void *info, DWORD bytes);
HttpResult http_start(HttpSlot *slot);
void http_finish(HttpSlot *slot, HttpResult result);
void http_read_next(HttpSlot *slot);
void http_on_headers(HttpSlot *slot);
bool http_query_header(HttpSlot *slot, DWORD query, const wchar_t *name,
    wchar_t *output, DWORD capacity);
void http_close_attempt(HttpSlot *slot, HttpAfterClose after);
uint64_t http_utc_ms(void);
#endif
