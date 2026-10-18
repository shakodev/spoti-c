#include "http_internal.h"
#include <string.h>
#include <wchar.h>

static HttpSlot *find_slot(HttpClient *client, HttpRequestId id)
{
    if (client == NULL || id.slot >= HTTP_SLOT_COUNT || id.generation == 0) { return NULL; }
    HttpSlot *slot = &client->slots[id.slot];
    return slot->state != HS_FREE && slot->generation == id.generation ? slot : NULL;
}

static bool copy_headers(HttpSlot *slot, const HttpRequest *request)
{
    static const wchar_t *const forbidden[] = {L"Host", L"Content-Length", L"Transfer-Encoding",
        L"Connection", L"Accept-Encoding", L"Proxy-Authorization", L"Upgrade", L"Expect", L"TE", L"Range"};
    size_t used = 0;
    for (size_t i = 0; i < request->header_count; ++i) {
        const wchar_t *name = request->headers[i].name, *value = request->headers[i].value;
        size_t n = 0, v = 0;
        if (name == NULL || value == NULL) { return false; }
        while (n < 64 && name[n] != L'\0') {
            wchar_t c = name[n++];
            if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                (c >= L'0' && c <= L'9') || wcschr(L"!#$%&'*+-.^_`|~", c) != NULL)) { return false; }
        }
        if (n == 0 || n == 64) { return false; }
        for (size_t f = 0; f < sizeof(forbidden) / sizeof(forbidden[0]); ++f) {
            if (_wcsicmp(name, forbidden[f]) == 0) { return false; }
        }
        while (v < 1024 && value[v] != L'\0') {
            if (value[v] < L' ' || value[v] == 127) { return false; }
            ++v;
        }
        if (v == 1024 || used + n + v + 4 >= HTTP_REQUEST_HEADERS) { return false; }
        memcpy(slot->headers + used, name, n * sizeof(wchar_t)); used += n;
        slot->headers[used++] = L':'; slot->headers[used++] = L' ';
        memcpy(slot->headers + used, value, v * sizeof(wchar_t)); used += v;
        slot->headers[used++] = L'\r'; slot->headers[used++] = L'\n';
    }
    slot->headers[used] = L'\0'; return true;
}

HttpResult http_submit(HttpClient *client, const HttpRequest *request, HttpRequestId *id)
{
    HttpOrigin origin;
    if (id == NULL) { return HTTP_INVALID; }
    *id = (HttpRequestId){0};
    if (client == NULL || request == NULL || request->method > HTTP_DELETE ||
        (int)request->method < 0 || request->header_count > 8 ||
        (request->header_count != 0 && request->headers == NULL) ||
        request->max_retries > 3 || request->max_redirects > 5 || request->max_expansion > 1024 ||
        request->upload_bytes > HTTP_API_BODY_LIMIT ||
        (request->upload_bytes != 0 && request->upload == NULL) ||
        ((request->method == HTTP_GET || request->method == HTTP_HEAD) && request->upload_bytes != 0) ||
        (request->body_capacity != 0 && request->body == NULL) ||
        (request->stream && request->body_capacity < HTTP_BLOCK_SIZE) ||
        (request->range && (request->method != HTTP_GET || request->range_first > request->range_last)) ||
        !http_origin_parse(request->url, &origin, NULL, 0)) { return HTTP_INVALID; }
    if (client->stopping) { return HTTP_CLOSED; }
    if (!http_origin_valid(client, &origin) ||
        (request->header_count != 0 && !http_destination_allowed(client, &origin))) { return HTTP_DESTINATION; }
    HttpSlot *slot = NULL;
    for (uint32_t i = 0; i < HTTP_SLOT_COUNT; ++i) {
        if (client->slots[i].state == HS_FREE && client->slots[i].generation != UINT32_MAX) {
            slot = &client->slots[i]; id->slot = i; break;
        }
    }
    if (slot == NULL) { return HTTP_FULL; }
    if (!copy_headers(slot, request)) {
        SecureZeroMemory(slot->headers, sizeof(slot->headers)); return HTTP_INVALID;
    }
    ++slot->generation; id->generation = slot->generation;
    slot->request = *request; slot->request.url = NULL; slot->request.headers = NULL;
    if (slot->request.body_limit == 0) { slot->request.body_limit = HTTP_API_BODY_LIMIT; }
    if (slot->request.max_expansion == 0) { slot->request.max_expansion = 16; }
    memcpy(slot->url, request->url, (wcslen(request->url) + 1) * sizeof(wchar_t));
    slot->attempt = slot->redirects = slot->retries = 0;
    slot->stripped = slot->delivered = slot->ready = false;
    slot->result = HTTP_PENDING;
    slot->deadline = core_deadline_after(core_clock_now(&client->config.clock),
        request->timeout_ms == 0 ? 30000 : request->timeout_ms);
    HttpResult result = http_start(slot);
    if (result != HTTP_OK) { http_finish(slot, result); }
    /* Admission succeeded. Even a synchronous OS failure has an owned final
     * completion; caller must consume it before recycling borrowed storage. */
    return HTTP_OK;
}

HttpResult http_cancel(HttpClient *client, HttpRequestId id)
{
    HttpSlot *slot = find_slot(client, id);
    if (slot == NULL) { return client == NULL ? HTTP_INVALID : HTTP_STALE; }
    if (slot->state == HS_DONE) { return HTTP_OK; }
    if (slot->state == HS_CLOSING) {
        slot->after_close = HA_FINISH; slot->result = HTTP_CANCELLED;
    } else { http_finish(slot, HTTP_CANCELLED); }
    return HTTP_OK;
}

static void read_complete(HttpSlot *slot, DWORD bytes)
{
    WINHTTP_REQUEST_STATS stats = {.cStats = WinHttpRequestStatLast};
    DWORD stats_bytes = sizeof(stats);
    bool got_stats = WinHttpQueryOption(slot->handle, WINHTTP_OPTION_REQUEST_STATS, &stats, &stats_bytes) != FALSE;
    /* Native decoding removes Content-Encoding/Length. Never silently accept
     * decoded data when the OS cannot supply the wire-byte accounting used by
     * the ratio limit (requires Windows 10 1903+). EOF errors are classified
     * below, including truncated responses whose stats are no longer available. */
    if (!got_stats && bytes != 0) { http_finish(slot, HTTP_ENCODING); return; }
    uint64_t compressed = stats.rgullStats[WinHttpResponseBodyCompressedSize];
    uint64_t ratio = slot->request.max_expansion;
    if (got_stats && compressed != 0 && compressed <= SIZE_MAX / ratio &&
        (slot->total > compressed * ratio || bytes > compressed * ratio - slot->total)) {
        http_finish(slot, HTTP_BODY_TOO_LARGE); return;
    }
    if (bytes == 0) {
        HttpResult result = slot->has_length && !slot->encoded && slot->total != slot->wire_length ?
            HTTP_TRUNCATED : HTTP_OK;
        http_finish(slot, result); return;
    }
    if (bytes > HTTP_READ_SIZE || bytes > slot->decoded_limit - slot->total) {
        http_finish(slot, HTTP_BODY_TOO_LARGE); return;
    }
    memcpy((unsigned char *)slot->request.body + slot->bytes, slot->read_buffer, bytes);
    slot->bytes += bytes; slot->total += bytes;
    if (slot->request.stream && slot->bytes == HTTP_BLOCK_SIZE) {
        slot->state = HS_BLOCK; slot->ready = true;
    } else { http_read_next(slot); }
}

static void dispatch(HttpSlot *slot, const HttpCallback *record)
{
    if (record->status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING && slot->state == HS_CLOSING) {
        slot->handle = NULL;
        if (slot->metric.owner != NULL) { (void)metrics_handle_release(&slot->metric); }
        http_host_release(slot);
        if (slot->after_close == HA_FINISH) { slot->state = HS_DONE; slot->ready = true; }
        else if (slot->after_close == HA_RETRY) { slot->state = HS_BACKOFF; }
        else {
            HttpResult result = http_start(slot);
            if (result != HTTP_OK) { http_finish(slot, result); }
        }
        return;
    }
    if (slot->state == HS_CLOSING || slot->state == HS_DONE) { return; }
    if (record->status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        HttpResult result = HTTP_OS_ERROR;
        if (record->error == ERROR_WINHTTP_TIMEOUT) { result = HTTP_TIMEOUT; }
        else if (record->error == ERROR_WINHTTP_HEADER_SIZE_OVERFLOW) { result = HTTP_HEADERS_TOO_LARGE; }
        else if (slot->state == HS_READ && slot->has_length && !slot->encoded &&
            slot->total < slot->wire_length) { result = HTTP_TRUNCATED; }
        http_finish(slot, result);
    } else if (record->status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && slot->state == HS_SEND) {
        slot->state = HS_HEADERS;
        if (!WinHttpReceiveResponse(slot->handle, NULL)) { http_finish(slot, HTTP_OS_ERROR); }
    } else if (record->status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE && slot->state == HS_HEADERS) {
        http_on_headers(slot);
    } else if (record->status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE && slot->state == HS_READ) {
        read_complete(slot, record->bytes);
    }
}

HttpResult http_pump(HttpClient *client)
{
    if (client == NULL || client->session == NULL) { return HTTP_INVALID; }
    uint64_t now = core_clock_now(&client->config.clock);
    for (size_t i = 0; i < HTTP_SLOT_COUNT; ++i) {
        HttpSlot *slot = &client->slots[i];
        if (slot->state == HS_FREE || slot->state == HS_DONE || slot->state == HS_CLOSING) { continue; }
        if (core_deadline_expired(now, slot->deadline)) { http_finish(slot, HTTP_TIMEOUT); }
        else if (slot->state == HS_BACKOFF && core_deadline_expired(now, slot->retry_at)) {
            HttpResult result = http_start(slot);
            if (result != HTTP_OK) { http_finish(slot, result); }
        }
    }
    HttpCallback record;
    if (http_callback_take(client, &record)) {
        HttpSlot *slot = find_slot(client, record.id);
        if (slot != NULL && slot->attempt == record.attempt) { dispatch(slot, &record); }
    }
    AcquireSRWLockShared(&client->lock);
    bool overflow = client->overflow;
    ReleaseSRWLockShared(&client->lock);
    return overflow ? HTTP_OS_ERROR : HTTP_OK;
}

HANDLE http_wait_handle(const HttpClient *client) { return client == NULL ? NULL : client->wake; }

uint32_t http_next_wait_ms(const HttpClient *client)
{
    if (client == NULL) { return INFINITE; }
    uint64_t deadline = CORE_CLOCK_NEVER;
    for (size_t i = 0; i < HTTP_SLOT_COUNT; ++i) {
        const HttpSlot *slot = &client->slots[i];
        if (slot->state == HS_FREE || slot->state == HS_DONE || slot->state == HS_CLOSING) { continue; }
        if (slot->deadline < deadline) { deadline = slot->deadline; }
        if (slot->state == HS_BACKOFF && slot->retry_at < deadline) { deadline = slot->retry_at; }
    }
    return core_deadline_wait(core_clock_now(&client->config.clock), deadline);
}

HttpResult http_wait(HttpClient *client, uint32_t max_wait_ms)
{
    if (client == NULL || client->wake == NULL) { return HTTP_INVALID; }
    uint32_t wait = http_next_wait_ms(client);
    if (max_wait_ms < wait) { wait = max_wait_ms; }
    DWORD result = WaitForSingleObject(client->wake, wait);
    if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT) { return HTTP_OS_ERROR; }
    return http_pump(client);
}

HttpResult http_consume(HttpClient *client, HttpRequestId id, HttpCompletion *output)
{
    if (output == NULL) { return HTTP_INVALID; }
    *output = (HttpCompletion){0};
    HttpSlot *slot = find_slot(client, id);
    if (slot == NULL) { return client == NULL ? HTTP_INVALID : HTTP_STALE; }
    if (!slot->ready) { return HTTP_PENDING; }
    *output = (HttpCompletion){id, slot->state == HS_DONE ? slot->result : HTTP_OK,
        slot->status, slot->attempt, slot->request.body, slot->bytes, slot->total, slot->state == HS_DONE,
        slot->response_headers};
    slot->ready = false;
    if (slot->state == HS_DONE) {
        SecureZeroMemory(slot->headers, sizeof(slot->headers));
        SecureZeroMemory(slot->url, sizeof(slot->url));
        slot->request = (HttpRequest){0}; slot->state = HS_FREE;
    } else { slot->delivered = true; slot->bytes = 0; http_read_next(slot); }
    return HTTP_OK;
}

HttpResult http_client_shutdown(HttpClient *client, uint32_t timeout_ms)
{
    if (client == NULL) { return HTTP_INVALID; }
    if (client->session == NULL) { return HTTP_CLOSED; }
    client->stopping = true;
    for (uint32_t i = 0; i < HTTP_SLOT_COUNT; ++i) {
        if (client->slots[i].state != HS_FREE) {
            (void)http_cancel(client, (HttpRequestId){i, client->slots[i].generation});
        }
    }
    uint64_t deadline = core_deadline_after(GetTickCount64(), timeout_ms);
    for (;;) {
        bool active = false;
        for (uint32_t i = 0; i < HTTP_SLOT_COUNT; ++i) {
            HttpSlot *slot = &client->slots[i];
            if (slot->state == HS_DONE) {
                HttpCompletion ignored;
                (void)http_consume(client, (HttpRequestId){i, slot->generation}, &ignored);
            } else if (slot->state != HS_FREE) { active = true; }
        }
        if (!active) { break; }
        uint32_t wait = core_deadline_wait(GetTickCount64(), deadline);
        if (wait == 0) { return HTTP_TIMEOUT; }
        HttpResult result = http_wait(client, wait);
        if (result != HTTP_OK) { return result; }
    }
    for (size_t i = 0; i < HTTP_HOST_COUNT; ++i) {
        HttpHost *host = &client->hosts[i];
        if (host->handle != NULL) {
            if (!WinHttpCloseHandle(host->handle)) { return HTTP_OS_ERROR; }
            host->handle = NULL;
            if (host->metric.owner != NULL) { (void)metrics_handle_release(&host->metric); }
        }
    }
    if (!WinHttpCloseHandle(client->session)) { return HTTP_OS_ERROR; }
    client->session = NULL;
    if (client->session_metric.owner != NULL) { (void)metrics_handle_release(&client->session_metric); }
    if (!CloseHandle(client->wake)) { return HTTP_OS_ERROR; }
    client->wake = NULL;
    if (client->wake_metric.owner != NULL) { (void)metrics_handle_release(&client->wake_metric); }
    return HTTP_OK;
}
