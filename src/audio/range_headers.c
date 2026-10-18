#include "range_internal.h"
#include <wchar.h>

static bool decimal(const wchar_t **text, uint64_t *out)
{
    const wchar_t *p = *text; uint64_t n = 0; bool digit = false;
    while (*p >= L'0' && *p <= L'9') {
        unsigned d = (unsigned)(*p++ - L'0'); if (n > (UINT64_MAX - d) / 10) { return false; }
        n = n * 10 + d; digit = true;
    }
    if (!digit) { return false; } *out = n; *text = p; return true;
}
static bool field(const wchar_t *headers, const wchar_t *name, wchar_t *out, size_t capacity, bool required)
{
    if (headers == NULL) { return false; } size_t n = wcslen(name), found = 0;
    const wchar_t *p = headers;
    while (*p != 0) {
        const wchar_t *end = wcsstr(p, L"\r\n"); if (end == NULL) { return false; }
        if ((size_t)(end - p) > n && _wcsnicmp(p, name, n) == 0 && p[n] == L':') {
            if (++found > 1) { return false; } const wchar_t *value = p + n + 1;
            while (value < end && (*value == L' ' || *value == L'\t')) { ++value; }
            const wchar_t *tail = end; while (tail > value && (tail[-1] == L' ' || tail[-1] == L'\t')) { --tail; }
            size_t count = (size_t)(tail - value); if (count >= capacity) { return false; }
            memcpy(out, value, count * sizeof(wchar_t)); out[count] = 0;
        }
        p = end + 2;
    }
    if (found == 0) { out[0] = 0; } return !required || found == 1;
}
RangeResult range_parse_response(const HttpCompletion *c, uint64_t first, uint64_t last,
    uint64_t *total, size_t *bytes, wchar_t etag[128])
{
    if (c->status == 200) { return RANGE_UNSUPPORTED; } /* Never cache an ignored Range. */
    if (c->status == 401 || c->status == 403) { return RANGE_EXPIRED; }
    if (c->status != 206 && c->status != 416) { return RANGE_HTTP_ERROR; }
    wchar_t value[256], encoding[64], type[128];
    if (!field(c->headers, L"Content-Range", value, 256, true) ||
        !field(c->headers, L"Content-Encoding", encoding, 64, false) ||
        !field(c->headers, L"Content-Type", type, 128, false) ||
        !field(c->headers, L"ETag", etag, 128, false) ||
        (encoding[0] != 0 && _wcsicmp(encoding, L"identity") != 0) ||
        _wcsnicmp(type, L"multipart/byteranges", 20) == 0) { return RANGE_HEADER; }
    size_t tag_length = wcslen(etag);
    if (tag_length != 0) {
        if (tag_length < 2 || etag[0] != L'"' || etag[tag_length - 1] != L'"') { return RANGE_HEADER; }
        for (size_t i = 1; i + 1 < tag_length; ++i) {
            if (etag[i] < 0x21 || etag[i] == L'"' || etag[i] > 0xff) { return RANGE_HEADER; }
        }
    }
    const wchar_t *p = value;
    if (_wcsnicmp(p, L"bytes ", 6) != 0) { return RANGE_HEADER; } p += 6;
    uint64_t begin, end, size;
    if (c->status == 416) {
        if (p[0] != L'*' || p[1] != L'/') { return RANGE_HEADER; } p += 2;
        if (!decimal(&p, &size) || *p != 0 || first < size) { return RANGE_HEADER; }
        *total = size; *bytes = 0; return RANGE_EOF;
    }
    if (!decimal(&p, &begin) || *p++ != L'-' || !decimal(&p, &end) || *p++ != L'/' ||
        !decimal(&p, &size) || *p != 0 || begin != first || end < begin || end > last || size <= end ||
        end != (last < size ? last : size - 1) || end - begin >= RANGE_BLOCK_BYTES) { return RANGE_HEADER; }
    if (c->result != HTTP_OK || c->bytes != (size_t)(end - begin + 1)) {
        return c->result == HTTP_ENCODING ? RANGE_HEADER : RANGE_TRUNCATED;
    }
    *total = size; *bytes = c->bytes; return RANGE_OK;
}
