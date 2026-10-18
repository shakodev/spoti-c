#ifndef SPOTIC_API_HTTP_CLIENT_H
#define SPOTIC_API_HTTP_CLIENT_H

#include "core/arena.h"
#include "core/clock.h"
#include "core/metrics.h"

#define HTTP_SLOT_COUNT 8u
#define HTTP_HOST_COUNT 8u
#define HTTP_HEADER_LIMIT 8192u
#define HTTP_API_BODY_LIMIT ((size_t)1024 * 1024)
#define HTTP_BLOCK_SIZE ((size_t)64 * 1024)
#define HTTP_URL_CHARS 2048u

typedef struct HttpClient HttpClient;
typedef struct HttpRequestId { uint32_t slot, generation; } HttpRequestId;
typedef enum HttpResult {
    HTTP_OK, HTTP_PENDING, HTTP_FULL, HTTP_INVALID, HTTP_STALE, HTTP_CLOSED,
    HTTP_NO_MEMORY, HTTP_OS_ERROR, HTTP_CANCELLED, HTTP_TIMEOUT,
    HTTP_HEADERS_TOO_LARGE, HTTP_BODY_TOO_LARGE, HTTP_TRUNCATED,
    HTTP_AUTH, HTTP_RATE_LIMIT, HTTP_SERVER_ERROR, HTTP_STATUS_ERROR,
    HTTP_REDIRECT, HTTP_ENCODING, HTTP_DESTINATION, HTTP_EXHAUSTED
} HttpResult;
typedef enum HttpMethod { HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_DELETE } HttpMethod;
typedef struct HttpHeader { const wchar_t *name, *value; } HttpHeader;
typedef struct HttpConfig {
    /* Exact origins, including scheme and optional port, no path. Custom
     * headers are treated as sensitive and require this allowlist. Copied. */
    const wchar_t *header_origins[HTTP_HOST_COUNT];
    size_t header_origin_count;
    bool allow_loopback_http; /* Explicit deterministic fixture opt-in only. */
    CoreClock clock;
    Metrics *metrics; /* Optional; must outlive shutdown. */
} HttpConfig;
typedef struct HttpRequest {
    const wchar_t *url;
    HttpMethod method;
    const HttpHeader *headers;
    size_t header_count; /* <= 8; bounded total serialized headers. */
    const void *upload;
    size_t upload_bytes; /* <= 1 MiB; borrowed through final consume. */
    void *body;
    size_t body_capacity;
    size_t body_limit; /* Zero: 1 MiB. Total decoded bytes, also for streams. */
    bool stream; /* Caller supplies a >=64 KiB block, reused after consume. */
    uint32_t timeout_ms; /* Zero: 30 s total, including retry/backpressure. */
    uint32_t max_retries; /* <= 3; GET/HEAD only; no replay after delivery. */
    uint32_t max_redirects; /* <= 5; GET/HEAD only, no TLS downgrade. */
    uint32_t max_expansion; /* Zero: 16. Decoded/native compressed byte cap. */
    /* Typed public representation headers do not need a sensitive-header origin
     * allowlist. They never carry credentials. Range is inclusive, GET only. */
    bool range, identity_encoding;
    uint64_t range_first, range_last;
} HttpRequest;
typedef struct HttpCompletion {
    HttpRequestId id;
    HttpResult result;
    unsigned status, attempts;
    void *body; /* Borrowed caller buffer; bytes for this block/body. */
    size_t bytes, total_bytes;
    bool final;
    /* Borrowed bounded raw response headers. Copy needed fields before the
     * next submit that can reuse this slot, or before shutdown. */
    const wchar_t *headers;
} HttpCompletion;

/* One shared HTTP arena <=384 KiB; metadata, queues and buffers fit in it.
 * Explicit body/upload arenas are separate. No heap or thread is created by
 * this module. WinHTTP/OS internal allocations are outside this ledger.
 * All calls except WinHTTP callbacks belong to ONE network owner thread.
 * Keep arena/config clock/metrics and borrowed buffers alive until shutdown. */
HttpResult http_client_create(Arena *arena, const HttpConfig *config, HttpClient **output);
HttpResult http_submit(HttpClient *client, const HttpRequest *request, HttpRequestId *id);
HttpResult http_cancel(HttpClient *client, HttpRequestId id);
/* Process ONE immutable callback plus due deadlines; never blocks. */
HttpResult http_pump(HttpClient *client);
/* Blocks on callbacks or nearest deadline, then pumps once. INFINITE is safe
 * when idle. No periodic timer/polling; caller may instead use the borrowed
 * wait handle + http_next_wait_ms with MsgWaitForMultipleObjectsEx. */
HttpResult http_wait(HttpClient *client, uint32_t max_wait_ms);
HANDLE http_wait_handle(const HttpClient *client);
uint32_t http_next_wait_ms(const HttpClient *client);
/* Pending until terminal HANDLE_CLOSING has retired. Intermediate stream
 * blocks pause reads until consumed. A successful final consume frees slot;
 * old IDs then return STALE. Buffers may be released only after final consume. */
HttpResult http_consume(HttpClient *client, HttpRequestId id, HttpCompletion *output);
/* Closes admission, cancels requests, discards completions, waits for callback
 * retirement and closes hosts/session/event. TIMEOUT retains live storage for
 * another call. Success invalidates client; now arena may be reclaimed. */
HttpResult http_client_shutdown(HttpClient *client, uint32_t timeout_ms);

HttpResult http_classify_status(unsigned status);
uint32_t http_retry_delay_ms(unsigned retry, const wchar_t *retry_after, uint64_t utc_ms);

#endif
