#include "artwork.h"
#include <winhttp.h>
#include <string.h>
#include <wchar.h>

typedef struct ArtHttpHandle {
    HINTERNET value;
    MetricsHandle lease;
} ArtHttpHandle;

/* Native timeout phases are cooperative, not hard OS-call preemption. The
 * elapsed deadline is checked before/after network calls. Restrict each of
 * resolve/connect/send/receive to a quarter of the remaining job budget. */
static ArtResult http_budget(HINTERNET request, ULONGLONG started, const CancelToken *cancel)
{
    if (worker_cancelled(cancel)) { return ART_CANCELLED; }
    ULONGLONG elapsed = GetTickCount64() - started;
    if (elapsed >= 5000) { return ART_IO_ERROR; }
    DWORD remaining = (DWORD)(5000 - elapsed);
    DWORD phase = remaining / 4;
    if (phase == 0) { phase = 1; }
    if (phase > 1000) { phase = 1000; }
    if (!WinHttpSetTimeouts(request, (int)phase, (int)phase, (int)phase, (int)phase) ||
        !WinHttpSetOption(request, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &phase, sizeof(phase))) {
        return ART_IO_ERROR;
    }
    return ART_OK;
}

static ArtResult http_check(ULONGLONG started, const CancelToken *cancel)
{
    if (worker_cancelled(cancel)) { return ART_CANCELLED; }
    return GetTickCount64() - started >= 5000 ? ART_IO_ERROR : ART_OK;
}

static ArtResult http_adopt(ArtHttpHandle *handle, HINTERNET value, Metrics *metrics)
{
    handle->value = value;
    if (value == NULL) { return ART_IO_ERROR; }
    if (metrics != NULL && !metrics_handle_acquire(metrics, &handle->lease, (uintptr_t)value)) {
        return ART_BUDGET;
    }
    return ART_OK;
}

static bool http_close(ArtHttpHandle *handle)
{
    if (handle->value == NULL) { return true; }
    if (!WinHttpCloseHandle(handle->value)) { return false; }
    handle->value = NULL;
    return handle->lease.owner == NULL || metrics_handle_release(&handle->lease);
}

static bool http_url(const wchar_t *url, wchar_t *host, wchar_t *path, INTERNET_PORT *port)
{
    size_t length = 0;
    while (length < ART_URL_CHARS && url[length] != L'\0') {
        /* Fragment is local browser state; neither it nor raw controls belong
         * in an image request. WinHTTP performs its own normal escaping. */
        if (url[length] <= L' ' || url[length] == L'#') { return false; }
        ++length;
    }
    if (length == 0 || length == ART_URL_CHARS) { return false; }
    URL_COMPONENTS parts = {0};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = parts.dwHostNameLength = parts.dwUrlPathLength = (DWORD)-1;
    parts.dwUserNameLength = parts.dwPasswordLength = parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url, (DWORD)length, ICU_REJECT_USERPWD, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwHostNameLength == 0 ||
        parts.dwHostNameLength >= ART_URL_CHARS ||
        parts.dwUserNameLength != 0 || parts.dwPasswordLength != 0) { return false; }
    memcpy(host, parts.lpszHostName, (size_t)parts.dwHostNameLength * sizeof(*host));
    host[parts.dwHostNameLength] = L'\0';
    size_t used = parts.dwUrlPathLength;
    if (used == 0) { path[used++] = L'/'; }
    else { memcpy(path, parts.lpszUrlPath, used * sizeof(*path)); }
    if (parts.dwExtraInfoLength >= ART_URL_CHARS - used) { return false; }
    if (parts.dwExtraInfoLength != 0) {
        memcpy(path + used, parts.lpszExtraInfo, (size_t)parts.dwExtraInfoLength * sizeof(*path));
        used += parts.dwExtraInfoLength;
    }
    path[used] = L'\0';
    *port = parts.nPort;
    return true;
}

static ArtResult http_length(HINTERNET request, size_t capacity, size_t *length, bool *present)
{
    wchar_t header[32] = {0};
    DWORD bytes = sizeof(header);
    *present = false;
    *length = 0;
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
        header, &bytes, WINHTTP_NO_HEADER_INDEX)) {
        DWORD error = GetLastError();
        if (error == ERROR_WINHTTP_HEADER_NOT_FOUND) { return ART_OK; }
        return error == ERROR_INSUFFICIENT_BUFFER ? ART_TOO_LARGE : ART_IO_ERROR;
    }
    if (header[0] == L'\0') { return ART_IO_ERROR; }
    for (size_t i = 0; header[i] != L'\0'; ++i) {
        if (header[i] < L'0' || header[i] > L'9') { return ART_IO_ERROR; }
        size_t digit = (size_t)(header[i] - L'0');
        if (digit > capacity || *length > (capacity - digit) / 10) { return ART_TOO_LARGE; }
        *length = *length * 10 + digit;
    }
    *present = true;
    return ART_OK;
}

static ArtResult http_identity(HINTERNET request)
{
    wchar_t encoding[32] = {0};
    DWORD bytes = sizeof(encoding);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_ENCODING, WINHTTP_HEADER_NAME_BY_INDEX,
        encoding, &bytes, WINHTTP_NO_HEADER_INDEX)) {
        return GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND ? ART_OK : ART_UNSUPPORTED;
    }
    return _wcsicmp(encoding, L"identity") == 0 ? ART_OK : ART_UNSUPPORTED;
}

static ArtResult http_body(HINTERNET request, void *buffer, size_t capacity, size_t expected,
    bool has_length, size_t *bytes, ULONGLONG started, const CancelToken *cancel)
{
    size_t total = 0;
    for (;;) {
        ArtResult result = http_budget(request, started, cancel);
        if (result != ART_OK) { return result; }
        DWORD available = 0;
        BOOL ok = WinHttpQueryDataAvailable(request, &available);
        result = http_check(started, cancel);
        if (result != ART_OK) { return result; }
        if (!ok) { return ART_IO_ERROR; }
        if (available == 0) {
            if (total == 0 || (has_length && total != expected)) { return ART_IO_ERROR; }
            *bytes = total;
            return ART_OK;
        }
        if ((size_t)available > capacity - total) { return ART_TOO_LARGE; }
        /* Read only bytes already available, avoiding fill-buffer waits over
         * many individual receive packets. No full-response heap copy. */
        DWORD read = 0;
        ok = WinHttpReadData(request, (unsigned char *)buffer + total, available, &read);
        result = http_check(started, cancel);
        if (result != ART_OK) { return result; }
        if (!ok || read == 0 || read > available) { return ART_IO_ERROR; }
        total += read;
        if (has_length && total > expected) { return ART_IO_ERROR; }
    }
}

ArtResult artwork_http_fetch(void *context, const wchar_t *url, void *buffer,
    size_t capacity, size_t *bytes, const CancelToken *cancel)
{
    if (bytes != NULL) { *bytes = 0; }
    if (url == NULL || buffer == NULL || bytes == NULL || capacity == 0 ||
        capacity > ART_COMPRESSED_BYTES || cancel == NULL) { return ART_INVALID; }
    wchar_t host[ART_URL_CHARS], path[ART_URL_CHARS];
    INTERNET_PORT port;
    if (!http_url(url, host, path, &port)) { return ART_INVALID; }
    if (worker_cancelled(cancel)) { return ART_CANCELLED; }
    ULONGLONG started = GetTickCount64();
    Metrics *metrics = context;
    ArtHttpHandle session = {0}, connection = {0}, request = {0};
    ArtResult result = http_adopt(&session, WinHttpOpen(L"spoti-c/artwork", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), metrics);
    if (result != ART_OK) { goto finish; }
    DWORD retries = 1;
    BOOL private_pool = TRUE;
    if (!WinHttpSetTimeouts(session.value, 1000, 1000, 1000, 1000) ||
        !WinHttpSetOption(session.value, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries)) ||
        !WinHttpSetOption(session.value, WINHTTP_OPTION_DISABLE_GLOBAL_POOLING, &private_pool, sizeof(private_pool))) {
        result = ART_IO_ERROR; goto finish;
    }
    result = http_adopt(&connection, WinHttpConnect(session.value, host, port, 0), metrics);
    if (result != ART_OK) { goto finish; }
    result = http_adopt(&request, WinHttpOpenRequest(connection.value, L"GET", path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), metrics);
    if (result != ART_OK) { goto finish; }
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES |
        WINHTTP_DISABLE_AUTHENTICATION | WINHTTP_DISABLE_KEEP_ALIVE;
    DWORD headers = 8192, drain = 0, decompress = 0;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_RESPONSE_HEADER_SIZE, &headers, sizeof(headers)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_RESPONSE_DRAIN_SIZE, &drain, sizeof(drain)) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress))) {
        result = ART_IO_ERROR; goto finish;
    }
    result = http_budget(request.value, started, cancel);
    if (result != ART_OK) { goto finish; }
    BOOL ok = WinHttpSendRequest(request.value, L"Accept-Encoding: identity\r\n", (DWORD)-1,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    result = http_check(started, cancel);
    if (result != ART_OK) { goto finish; }
    if (!ok) { result = ART_IO_ERROR; goto finish; }
    result = http_budget(request.value, started, cancel);
    if (result != ART_OK) { goto finish; }
    ok = WinHttpReceiveResponse(request.value, NULL);
    result = http_check(started, cancel);
    if (result != ART_OK) { goto finish; }
    if (!ok) { result = ART_IO_ERROR; goto finish; }
    DWORD status = 0, status_bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_bytes, WINHTTP_NO_HEADER_INDEX) || status != 200) {
        result = ART_IO_ERROR; goto finish;
    }
    size_t expected = 0;
    bool has_length = false;
    result = http_length(request.value, capacity, &expected, &has_length);
    if (result != ART_OK) { goto finish; }
    result = http_identity(request.value);
    if (result != ART_OK) { goto finish; }
    result = http_body(request.value, buffer, capacity, expected, has_length, bytes, started, cancel);
finish:
    /* There is no callback that can outlive these synchronous calls or write
     * the borrowed body buffer after the job completes. Retire in child order. */
    if (!http_close(&request)) { result = ART_IO_ERROR; }
    if (!http_close(&connection)) { result = ART_IO_ERROR; }
    if (!http_close(&session)) { result = ART_IO_ERROR; }
    if (result != ART_OK) { *bytes = 0; }
    return result;
}
