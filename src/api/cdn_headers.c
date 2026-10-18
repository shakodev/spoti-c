#include "cdn_internal.h"
#include <ctype.h>
static const char *line_end(const char *p, const char *end)
{
    for (; end-p >= 2; ++p) { if (p[0] == '\r' && p[1] == '\n') { return p; } }
    return NULL;
}
static bool name_is(const char *p, size_t n, const char *name)
{
    return strlen(name) == n && !_strnicmp(p,name,n);
}
HttpResult cdn_headers_parse(const char *text, size_t bytes, size_t capacity,
    unsigned *status, size_t *body, wchar_t *wide, size_t wide_capacity)
{
    if (!text || !status || !body || !wide || bytes > HTTP_HEADER_LIMIT || bytes >= wide_capacity ||
        bytes < 16 || memcmp(text+bytes-4,"\r\n\r\n",4)) { return HTTP_HEADERS_TOO_LARGE; }
    for (size_t i = 0; i < bytes; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (!ch || (ch < 32 && ch != '\r' && ch != '\n' && ch != '\t') || ch == 127) { return HTTP_INVALID; }
        if ((ch == '\r' && (i+1 == bytes || text[i+1] != '\n')) ||
            (ch == '\n' && (!i || text[i-1] != '\r'))) { return HTTP_INVALID; }
        wide[i] = ch;
    }
    wide[bytes] = 0; const char *end = text+bytes, *line = line_end(text,end);
    if (!line || line-text < 12 || (memcmp(text,"HTTP/1.1 ",9) && memcmp(text,"HTTP/1.0 ",9)) ||
        text[9] < '1' || text[9] > '5' || text[10] < '0' || text[10] > '9' ||
        text[11] < '0' || text[11] > '9' || (line-text > 12 && text[12] != ' ')) { return HTTP_INVALID; }
    *status = (unsigned)(text[9]-'0')*100+(unsigned)(text[10]-'0')*10+(unsigned)(text[11]-'0');
    bool length_seen = false, encoding_seen = false; size_t length = 0;
    const char *p = line+2;
    while (p < end) {
        line = line_end(p,end); if (!line) { return HTTP_INVALID; }
        if (line == p) { if (line+2 != end) { return HTTP_INVALID; } break; }
        const char *colon = p;
        while (colon < line && *colon != ':') {
            unsigned char ch = (unsigned char)*colon;
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || strchr("!#$%&'*+-.^_`|~",ch))) { return HTTP_INVALID; }
            ++colon;
        }
        if (colon == p || colon == line) { return HTTP_INVALID; }
        const char *value = colon+1, *tail = line;
        while (value < tail && (*value == ' ' || *value == '\t')) { ++value; }
        while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t')) { --tail; }
        for (const char *q = value; q < tail; ++q) { if (*q == '\r' || *q == '\n') { return HTTP_INVALID; } }
        size_t name = (size_t)(colon-p), count = (size_t)(tail-value);
        if (name_is(p,name,"Content-Length")) {
            if (length_seen || !count) { return HTTP_INVALID; } length_seen = true;
            for (const char *q = value; q < tail; ++q) {
                if (*q < '0' || *q > '9' || length > (SIZE_MAX-(unsigned)(*q-'0'))/10) { return HTTP_INVALID; }
                length = length*10+(unsigned)(*q-'0');
            }
        } else if (name_is(p,name,"Transfer-Encoding")) { return HTTP_ENCODING; }
        else if (name_is(p,name,"Content-Encoding")) {
            if (encoding_seen || count != 8 || _strnicmp(value,"identity",8)) { return HTTP_ENCODING; }
            encoding_seen = true;
        }
        p = line+2;
    }
    if (!length_seen) { return HTTP_INVALID; }
    if (length > capacity) { return HTTP_BODY_TOO_LARGE; }
    *body = length; return HTTP_OK;
}
