#include "http_internal.h"
#include <string.h>
#include <wchar.h>

static bool decimal_size(const wchar_t *text, size_t *value)
{
    size_t n = 0;
    if (*text == L'\0') { return false; }
    for (; *text != L'\0'; ++text) {
        if (*text < L'0' || *text > L'9' || n > (SIZE_MAX - (unsigned)(*text - L'0')) / 10) {
            return false;
        }
        n = n * 10 + (unsigned)(*text - L'0');
    }
    *value = n; return true;
}

static void redirect(HttpSlot *slot)
{
    wchar_t location[HTTP_URL_CHARS], next[HTTP_URL_CHARS];
    HttpOrigin before, after;
    if ((slot->request.method != HTTP_GET && slot->request.method != HTTP_HEAD) ||
        slot->redirects >= slot->request.max_redirects ||
        !(slot->status == 301 || slot->status == 302 || slot->status == 303 ||
          slot->status == 307 || slot->status == 308) ||
        !http_query_header(slot, WINHTTP_QUERY_LOCATION, NULL, location, sizeof(location))) {
        http_finish(slot, HTTP_REDIRECT); return;
    }
    /* Accept absolute URLs and root-relative paths. Reject ambiguous relative
     * paths instead of guessing an origin. Scheme-relative URLs are rejected. */
    if (location[0] == L'/' && location[1] != L'/') {
        const wchar_t *authority = wcsstr(slot->url, L"://");
        if (authority == NULL) { http_finish(slot, HTTP_REDIRECT); return; }
        const wchar_t *end = wcspbrk(authority + 3, L"/?");
        size_t prefix = end == NULL ? wcslen(slot->url) : (size_t)(end - slot->url);
        size_t tail = wcslen(location);
        if (prefix + tail >= HTTP_URL_CHARS) { http_finish(slot, HTTP_REDIRECT); return; }
        memcpy(next, slot->url, prefix * sizeof(wchar_t));
        memcpy(next + prefix, location, (tail + 1) * sizeof(wchar_t));
    } else { memcpy(next, location, (wcslen(location) + 1) * sizeof(wchar_t)); }
    if (!http_origin_parse(slot->url, &before, NULL, 0) ||
        !http_origin_parse(next, &after, NULL, 0) || !http_origin_valid(slot->client, &after) ||
        (before.secure && !after.secure)) { http_finish(slot, HTTP_DESTINATION); return; }
    if (!http_origin_equal(&before, &after)) {
        /* Clear every caller header, not just Authorization. Never restore them
         * even if a subsequent redirect returns to an allowlisted origin. */
        slot->stripped = true;
        SecureZeroMemory(slot->headers, sizeof(slot->headers));
    }
    memcpy(slot->url, next, (wcslen(next) + 1) * sizeof(wchar_t));
    ++slot->redirects;
    http_close_attempt(slot, HA_REDIRECT);
}

void http_on_headers(HttpSlot *slot)
{
    DWORD status, bytes = sizeof(status);
    wchar_t value[HTTP_HEADER_LIMIT / sizeof(wchar_t)];
    if (!WinHttpQueryHeaders(slot->handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        NULL, &status, &bytes, WINHTTP_NO_HEADER_INDEX)) { http_finish(slot, HTTP_OS_ERROR); return; }
    slot->status = status;
    if (!http_query_header(slot, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL,
        slot->response_headers, sizeof(slot->response_headers))) {
        http_finish(slot, HTTP_HEADERS_TOO_LARGE); return;
    }
    HttpResult result = http_classify_status(status);
    if (result == HTTP_REDIRECT) { redirect(slot); return; }
    if ((result == HTTP_RATE_LIMIT || result == HTTP_SERVER_ERROR) && !slot->delivered &&
        (slot->request.method == HTTP_GET || slot->request.method == HTTP_HEAD) &&
        slot->retries < slot->request.max_retries) {
        (void)http_query_header(slot, WINHTTP_QUERY_RETRY_AFTER, NULL, value, sizeof(value));
        uint32_t delay = http_retry_delay_ms(slot->retries++, value, http_utc_ms());
        slot->retry_at = core_deadline_after(core_clock_now(&slot->client->config.clock), delay);
        http_close_attempt(slot, HA_RETRY); return;
    }
    if (result != HTTP_OK) { http_finish(slot, result); return; }
    if (http_query_header(slot, WINHTTP_QUERY_CONTENT_LENGTH, NULL, value, sizeof(value))) {
        if (!decimal_size(value, &slot->wire_length)) { http_finish(slot, HTTP_TRUNCATED); return; }
        slot->has_length = true;
    }
    if (http_query_header(slot, WINHTTP_QUERY_CONTENT_ENCODING, NULL, value, sizeof(value)) &&
        _wcsicmp(value, L"identity") != 0) {
        if (slot->request.identity_encoding) { http_finish(slot, HTTP_ENCODING); return; }
        if ((_wcsicmp(value, L"gzip") != 0 && _wcsicmp(value, L"deflate") != 0) ||
            !slot->has_length || slot->wire_length == 0) { http_finish(slot, HTTP_ENCODING); return; }
        slot->encoded = true;
        size_t ratio = slot->request.max_expansion;
        if (slot->wire_length <= SIZE_MAX / ratio && slot->decoded_limit > slot->wire_length * ratio) {
            slot->decoded_limit = slot->wire_length * ratio;
        }
    }
    if (slot->request.method == HTTP_HEAD) { http_finish(slot, HTTP_OK); return; }
    if (slot->has_length && !slot->encoded && slot->wire_length > slot->decoded_limit) {
        http_finish(slot, HTTP_BODY_TOO_LARGE); return;
    }
    http_read_next(slot);
}
