#include "token_internal.h"
#include <string.h>
#include <wchar.h>

bool token_text_valid(const char *s, size_t cap, size_t maximum)
{
    if (s == NULL) { return false; }
    size_t n = strnlen_s(s, cap);
    if (n == 0 || n == cap || n > maximum) { return false; }
    for (size_t i = 0; i < n; ++i) { if ((unsigned char)s[i] <= 32 || (unsigned char)s[i] >= 127) { return false; } }
    return true;
}
bool token_domain_valid(const char *domain)
{
    if (domain == NULL) { return false; }
    if (*domain == '.') { ++domain; }
    size_t n = strnlen_s(domain, 254), label = 0;
    if (n == 0 || n == 254) { return false; }
    for (size_t i = 0; i < n; ++i) {
        char c = domain[i];
        if (c == '.') {
            if (label == 0 || domain[i - 1] == '-') { return false; } label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') ||
                (label == 0 && c == '-') || ++label > 63) { return false; }
        }
    }
    return label != 0 && domain[n - 1] != '-';
}
bool token_url_host(const wchar_t *url, bool origin_only, bool fixture, char host[256])
{
    if (url == NULL || wcsnlen_s(url, HTTP_URL_CHARS) == HTTP_URL_CHARS) { return false; }
    const wchar_t *p = url;
    if (fixture) {
        if (wcsncmp(p, L"http://127.0.0.1:", 17) != 0) { return false; }
        p += 17; unsigned port = 0; const wchar_t *start = p;
        while (*p >= L'0' && *p <= L'9') { port = port * 10 + (unsigned)(*p++ - L'0'); if (port > 65535) { return false; } }
        if (p == start || port == 0 || *p != L'/') { return false; }
        memcpy(host, "127.0.0.1", 10);
    } else {
        if (wcsncmp(p, L"https://", 8) != 0) { return false; } p += 8;
        size_t n = 0;
        while (*p != 0 && *p != L'/') {
            if (*p > 127 || n == 255) { return false; } host[n++] = (char)*p++;
        }
        host[n] = 0;
        if (!token_domain_valid(host) || host[0] == '.') { return false; }
        if (origin_only && *p != 0) { return false; }
    }
    for (; *p != 0; ++p) { if (*p <= L' ' || *p == L'#' || *p == L'\\') { return false; } }
    return true;
}
bool token_domain_granted(const TokenManager *m, const char *host)
{
    size_t n = strlen(host);
    for (size_t i = 0; i < m->domain_count; ++i) {
        const char *domain = m->domains[i]; if (*domain == '.') { ++domain; }
        size_t bytes = strlen(domain);
        if (n >= bytes && memcmp(host + n - bytes, domain, bytes) == 0 &&
            (n == bytes || host[n - bytes - 1] == '.')) { return true; }
    }
    return false;
}
static bool service_allowed(const TokenManager *m, const wchar_t *url, const char *host)
{
    for (size_t i = 0; i < m->config.service_origin_count; ++i) {
        const wchar_t *origin = m->config.service_origins[i]; size_t n = wcslen(origin);
        if (wcsncmp(url, origin, n) == 0 && (url[n] == 0 || url[n] == L'/')) { return token_domain_granted(m, host); }
    }
    return false;
}
void token_authorization_clear(TokenAuthorization *a) { if (a != NULL) { SecureZeroMemory(a, sizeof(*a)); } }
TokenResult token_authorize(TokenManager *m, const wchar_t *url, TokenAuthorization *a)
{
    if (m == NULL || a == NULL) { return TOKEN_INVALID; }
    SecureZeroMemory(a->bearer, sizeof(a->bearer)); SecureZeroMemory(a->client, sizeof(a->client)); a->issued = false;
    if (m->shutdown) { return TOKEN_CLOSED; }
    char host[256];
    if (!token_url_host(url, false, false, host)) { return TOKEN_SCOPE; }
    bool trusted = false;
    for (size_t i = 0; i < m->config.service_origin_count; ++i) {
        size_t n = wcslen(m->config.service_origins[i]);
        trusted = trusted || (wcsncmp(url, m->config.service_origins[i], n) == 0 && (url[n] == 0 || url[n] == L'/'));
    }
    if (!trusted) { return TOKEN_SCOPE; }
    if (a->retries > 1) { return TOKEN_REJECTED; }
    TokenResult result = token_ensure(m, TOKEN_BEARER);
    if (result != TOKEN_OK) { return result; }
    if (!service_allowed(m, url, host)) { return TOKEN_SCOPE; }
    memcpy(a->bearer, L"Bearer ", 7 * sizeof(wchar_t));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m->slots[TOKEN_BEARER].text, -1,
        a->bearer + 7, TOKEN_TEXT_CAP - 7) == 0 ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m->slots[TOKEN_CLIENT].text, -1, a->client, TOKEN_TEXT_CAP) == 0) {
        token_authorization_clear(a); return TOKEN_FULL;
    }
    a->headers[0] = (HttpHeader){L"Authorization", a->bearer}; a->headers[1] = (HttpHeader){L"client-token", a->client};
    a->client_serial = m->slots[TOKEN_CLIENT].info.serial; a->bearer_serial = m->slots[TOKEN_BEARER].info.serial;
    a->issued = true; return TOKEN_OK;
}
TokenResult token_report_status(TokenManager *m, TokenAuthorization *a, unsigned status)
{
    if (m == NULL || a == NULL || !a->issued) { return TOKEN_INVALID; }
    a->issued = false; SecureZeroMemory(a->bearer, sizeof(a->bearer)); SecureZeroMemory(a->client, sizeof(a->client));
    if (m->shutdown) { return TOKEN_STALE; }
    if (status == 403) { return TOKEN_FORBIDDEN; }
    if (status != 401) { return TOKEN_OK; }
    if (++a->retries > 1) { return TOKEN_REJECTED; }
    if (a->client_serial == m->slots[TOKEN_CLIENT].info.serial && a->bearer_serial == m->slots[TOKEN_BEARER].info.serial) {
        SecureZeroMemory(&m->slots[TOKEN_BEARER], sizeof(TokenSlot));
    }
    return TOKEN_RETRY;
}
