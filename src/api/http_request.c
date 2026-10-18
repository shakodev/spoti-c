#include "http_internal.h"
#include <string.h>
#include <wchar.h>

static HttpResult last_error(void)
{
    DWORD error = GetLastError();
    if (error == ERROR_WINHTTP_TIMEOUT) { return HTTP_TIMEOUT; }
    if (error == ERROR_WINHTTP_HEADER_SIZE_OVERFLOW) { return HTTP_HEADERS_TOO_LARGE; }
    return HTTP_OS_ERROR;
}

void http_close_attempt(HttpSlot *slot, HttpAfterClose after)
{
    slot->after_close = after; slot->state = HS_CLOSING; slot->ready = false;
    if (slot->handle != NULL) {
        /* The owner never calls a WinHTTP operation on this handle again. */
        if (!WinHttpCloseHandle(slot->handle)) { slot->result = HTTP_OS_ERROR; }
    } else {
        http_host_release(slot);
        slot->state = after == HA_RETRY ? HS_BACKOFF : HS_DONE;
        slot->ready = after == HA_FINISH;
    }
}

void http_finish(HttpSlot *slot, HttpResult result)
{
    slot->result = result;
    http_close_attempt(slot, HA_FINISH);
}

HttpResult http_start(HttpSlot *slot)
{
    static const wchar_t *const methods[] = {L"GET", L"HEAD", L"POST", L"PUT", L"DELETE"};
    wchar_t path[HTTP_URL_CHARS];
    HttpOrigin origin;
    HttpClient *client = slot->client;
    if (!http_origin_parse(slot->url, &origin, path, HTTP_URL_CHARS) ||
        !http_origin_valid(client, &origin) ||
        (!slot->stripped && slot->headers[0] != L'\0' && !http_destination_allowed(client, &origin))) {
        return HTTP_DESTINATION;
    }
    HttpResult result = http_host_acquire(client, &origin, &slot->host);
    if (result != HTTP_OK) { return result; }
    ++slot->attempt;
    slot->status = 0; slot->bytes = 0; slot->total = 0;
    slot->response_headers[0] = L'\0';
    slot->has_length = slot->encoded = false;
    slot->decoded_limit = slot->request.body_limit;
    if (!slot->request.stream && slot->decoded_limit > slot->request.body_capacity) {
        slot->decoded_limit = slot->request.body_capacity;
    }
    slot->handle = WinHttpOpenRequest(slot->host->handle, methods[slot->request.method], path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, origin.secure ? WINHTTP_FLAG_SECURE : 0);
    if (slot->handle == NULL) { http_host_release(slot); return HTTP_OS_ERROR; }
    if (client->config.metrics != NULL && !metrics_handle_acquire(client->config.metrics,
        &slot->metric, (uintptr_t)slot->handle)) {
        WinHttpCloseHandle(slot->handle); slot->handle = NULL;
        http_host_release(slot); return HTTP_EXHAUSTED;
    }
    DWORD_PTR context = (DWORD_PTR)slot;
    if (!WinHttpSetOption(slot->handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
        WinHttpCloseHandle(slot->handle); slot->handle = NULL;
        if (slot->metric.owner != NULL) { (void)metrics_handle_release(&slot->metric); }
        http_host_release(slot); return HTTP_OS_ERROR;
    }
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD header_limit = HTTP_HEADER_LIMIT, drain = 0;
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    if (!WinHttpSetOption(slot->handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
        !WinHttpSetOption(slot->handle, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &header_limit, sizeof(header_limit)) ||
        !WinHttpSetOption(slot->handle, WINHTTP_OPTION_MAX_RESPONSE_DRAIN_SIZE, &drain, sizeof(drain)) ||
        !WinHttpSetOption(slot->handle, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress)) ||
        !WinHttpSetTimeouts(slot->handle, 5000, 5000, 5000, 5000) ||
        (!slot->stripped && slot->headers[0] != L'\0' && !WinHttpAddRequestHeaders(slot->handle,
            slot->headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD))) { return last_error(); }
    slot->state = HS_SEND;
    if (!WinHttpSendRequest(slot->handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (void *)slot->request.upload, (DWORD)slot->request.upload_bytes,
        (DWORD)slot->request.upload_bytes, context)) { return last_error(); }
    return HTTP_OK;
}

void http_read_next(HttpSlot *slot)
{
    DWORD bytes = HTTP_READ_SIZE;
    if (slot->request.stream && HTTP_BLOCK_SIZE - slot->bytes < bytes) {
        bytes = (DWORD)(HTTP_BLOCK_SIZE - slot->bytes);
    }
    slot->state = HS_READ;
    if (!WinHttpReadData(slot->handle, slot->read_buffer, bytes, NULL)) {
        http_finish(slot, last_error());
    }
}

bool http_query_header(HttpSlot *slot, DWORD query, const wchar_t *name,
    wchar_t *output, DWORD capacity)
{
    output[0] = L'\0';
    return WinHttpQueryHeaders(slot->handle, query, name, output, &capacity, WINHTTP_NO_HEADER_INDEX) != FALSE;
}
