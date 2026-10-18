#include "cdn_internal.h"
#include <wchar.h>
bool cdn_url_parse(const wchar_t *text, bool loopback, CdnUrl *out)
{
    if (!text || !out) { return false; }
    size_t n = wcsnlen_s(text, HTTP_URL_CHARS);
    if (!n || n == HTTP_URL_CHARS) { return false; }
    for (size_t i = 0; i < n; ++i) {
        if (text[i] <= 32 || text[i] > 126 || text[i] == L'#' || text[i] == L'\\') { return false; }
    }
    CdnUrl u = {0}; const wchar_t *p;
    if (!wcsncmp(text,L"https://",8)) { u.tls = true; u.port = 443; p = text + 8; }
    else if (loopback && !wcsncmp(text,L"http://",7)) { u.port = 80; p = text + 7; }
    else { return false; }
    const wchar_t *authority = p, *host = p; size_t host_bytes;
    if (*p == L'[') {
        host = ++p;
        while (*p && *p != L']') { ++p; }
        if (*p != L']') { return false; } host_bytes = (size_t)(p - host); ++p;
        IN6_ADDR address; wchar_t numeric[256];
        if (!host_bytes || host_bytes >= 256) { return false; }
        wmemcpy(numeric,host,host_bytes); numeric[host_bytes] = 0;
        if (InetPtonW(AF_INET6,numeric,&address) != 1) { return false; }
    } else {
        while ((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') ||
            (*p >= L'0' && *p <= L'9') || *p == L'.' || *p == L'-') { ++p; }
        host_bytes = (size_t)(p - host);
    }
    if (!host_bytes || host_bytes >= 256) { return false; }
    if (*p == L':') {
        unsigned port = 0; ++p; const wchar_t *start = p;
        while (*p >= L'0' && *p <= L'9') {
            unsigned digit = (unsigned)(*p++ - L'0');
            if (port > (65535u - digit) / 10) { return false; } port = port * 10 + digit;
        }
        if (p == start || !port) { return false; } u.port = (uint16_t)port;
    }
    if (*p && *p != L'/' && *p != L'?') { return false; }
    size_t authority_bytes = (size_t)(p - authority);
    if (authority_bytes >= sizeof(u.authority)) { return false; }
    for (size_t i = 0; i < authority_bytes; ++i) { u.authority[i] = (char)authority[i]; }
    wmemcpy(u.host,host,host_bytes); u.host[host_bytes] = 0;
    if (!u.tls && wcscmp(u.host,L"127.0.0.1")) { return false; }
    size_t used = 0;
    if (*p != L'/') { u.path[used++] = '/'; }
    while (*p) { if (used == sizeof(u.path) - 1) { return false; } u.path[used++] = (char)*p++; }
    u.path[used] = 0; *out = u; return true;
}
