#include "oauth_internal.h"
#include "core/memory_budget.h"
#include <shellapi.h>
#include <string.h>
#include <wchar.h>

_Static_assert(sizeof(OAuthFlow) <= MEMORY_BUDGET_PROTOCOL, "OAuth shares protocol scratch budget");
bool oauth_unreserved(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}
static bool copy_text(char *out, size_t cap, const char *in)
{
    if (in == NULL) { return false; }
    size_t n = strnlen_s(in, cap);
    if (n == cap) { return false; }
    memcpy(out, in, n + 1); return true;
}
static void base64url(const uint8_t *p, size_t n, char *out)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t bits = 0; unsigned count = 0; size_t at = 0;
    for (size_t i = 0; i < n; ++i) {
        bits = (bits << 8) | p[i]; count += 8;
        while (count >= 6) { count -= 6; out[at++] = alphabet[(bits >> count) & 63]; }
    }
    if (count != 0) { out[at++] = alphabet[(bits << (6 - count)) & 63]; }
    out[at] = 0;
}
OAuthResult oauth_pkce_challenge(const char *verifier, char challenge[44])
{
    if (challenge == NULL) { return OAUTH_INVALID; }
    challenge[0] = 0;
    size_t n = verifier == NULL ? 0 : strnlen_s(verifier, 129);
    if (n < 43 || n > 128) { return OAUTH_INVALID; }
    for (size_t i = 0; i < n; ++i) { if (!oauth_unreserved(verifier[i])) { return OAUTH_INVALID; } }
    Crypto crypto; crypto_init(&crypto); uint8_t digest[32];
    CryptoResult result = crypto_sha256(&crypto, verifier, n, digest);
    if (result == CRYPTO_OK) { base64url(digest, sizeof(digest), challenge); }
    SecureZeroMemory(digest, sizeof(digest));
    return result == CRYPTO_OK && crypto_balanced(&crypto) ? OAUTH_OK : OAUTH_IO_ERROR;
}
void oauth_write(OAuthWriter *w, const char *text)
{
    size_t n = strlen(text);
    if (w->full || n >= w->capacity - w->used) { w->full = true; return; }
    memcpy(w->data + w->used, text, n + 1); w->used += n;
}
void oauth_pair(OAuthWriter *w, const char *name, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    oauth_write(w, name); oauth_write(w, "=");
    for (const char *p = value; *p != 0; ++p) {
        unsigned char c = (unsigned char)*p;
        char encoded[4] = {'%', hex[c >> 4], hex[c & 15], 0};
        char literal[2] = {(char)c, 0};
        oauth_write(w, oauth_unreserved((char)c) ? literal : encoded);
    }
    oauth_write(w, "&");
}
static bool redirect_config(OAuthFlow *f)
{
    static const char prefix[] = "http://127.0.0.1";
    if (strncmp(f->registered, prefix, sizeof(prefix) - 1) != 0) { return false; }
    const char *p = f->registered + sizeof(prefix) - 1;
    if (*p == ':') {
        ++p; unsigned port = 0; const char *start = p;
        while (*p >= '0' && *p <= '9') { port = port * 10 + (unsigned)(*p++ - '0'); if (port > 65535) { return false; } }
        if (p == start || port == 0 || (*start == '0' && p - start > 1)) { return false; }
        f->port = (uint16_t)port;
    }
    if (*p != '/' || !copy_text(f->path, sizeof(f->path), p)) { return false; }
    for (; *p != 0; ++p) { if (!oauth_unreserved(*p) && *p != '/') { return false; } }
    return strstr(f->path, "..") == NULL && strstr(f->path, "//") == NULL;
}
static bool fixture_url_valid(const wchar_t *url)
{
    if (wcsncmp(url, L"http://127.0.0.1:", 17) != 0) { return false; }
    const wchar_t *p = url + 17, *start = p; unsigned port = 0;
    while (*p >= L'0' && *p <= L'9') { port = port * 10 + (unsigned)(*p++ - L'0'); if (port > 65535) { return false; } }
    if (p == start || port == 0 || *p != L'/' || (*start == L'0' && p - start > 1)) { return false; }
    for (; *p != 0; ++p) { if (*p != L'/' && (*p > 127 || !oauth_unreserved((char)*p))) { return false; } }
    return true;
}
OAuthResult oauth_create(Arena *arena, const OAuthConfig *c, OAuthFlow **output)
{
    if (output == NULL) { return OAUTH_INVALID; }
    *output = NULL;
    if (arena == NULL || arena->capacity > MEMORY_BUDGET_PROTOCOL || c == NULL || c->timeout_ms > 300000) { return OAUTH_INVALID; }
    ArenaMark mark = arena_mark(arena);
    OAuthFlow *f = arena_alloc(arena, sizeof(*f), _Alignof(OAuthFlow));
    if (f == NULL) { return OAUTH_FULL; }
    *f = (OAuthFlow){0}; f->listener = f->peer = INVALID_SOCKET;
    f->listen_event = f->peer_event = WSA_INVALID_EVENT;
    bool valid = copy_text(f->client_id, sizeof(f->client_id), c->client_id) && f->client_id[0] != 0 &&
        copy_text(f->registered, sizeof(f->registered), c->redirect_uri) && redirect_config(f) &&
        copy_text(f->scopes, sizeof(f->scopes), c->scopes == NULL ? "" : c->scopes);
    for (size_t i = 0; valid && f->client_id[i] != 0; ++i) { valid = oauth_unreserved(f->client_id[i]); }
    for (size_t i = 0; valid && f->scopes[i] != 0; ++i) { valid = oauth_unreserved(f->scopes[i]) || f->scopes[i] == ' '; }
    const wchar_t *url = c->fixture_token_url;
    if (url == NULL) { url = L"https://accounts.spotify.com/api/token"; }
    size_t n = wcsnlen_s(url, 256);
    if (n == 256 || (c->fixture_token_url != NULL && !fixture_url_valid(url))) { valid = false; }
    if (!valid) { SecureZeroMemory(f, sizeof(*f)); (void)arena_rewind(arena, mark); return OAUTH_INVALID; }
    memcpy(f->token_url, url, (n + 1) * sizeof(wchar_t));
    f->clock = c->clock; f->timeout_ms = c->timeout_ms == 0 ? 180000 : c->timeout_ms;
    f->deadline = f->peer_deadline = CORE_CLOCK_NEVER; *output = f; return OAUTH_OK;
}
void oauth_wipe_transient(OAuthFlow *f)
{
    SecureZeroMemory(f->verifier, sizeof(f->verifier)); SecureZeroMemory(f->state_key, sizeof(f->state_key));
    SecureZeroMemory(f->challenge, sizeof(f->challenge)); SecureZeroMemory(f->code, sizeof(f->code));
    SecureZeroMemory(f->authorization, sizeof(f->authorization)); SecureZeroMemory(f->request, sizeof(f->request));
    if (!f->http_active) {
        SecureZeroMemory(f->upload, sizeof(f->upload)); SecureZeroMemory(f->response, sizeof(f->response));
        SecureZeroMemory(f->json_pool, sizeof(f->json_pool));
    }
}
OAuthResult oauth_terminal(OAuthFlow *f, OAuthResult result)
{
    f->state = OAUTH_TERMINAL; f->result = result; f->deadline = CORE_CLOCK_NEVER;
    oauth_loopback_close(f); oauth_wipe_transient(f);
    SecureZeroMemory(f->refresh, sizeof(f->refresh)); SecureZeroMemory(&f->token, sizeof(f->token));
    if (f->http_active) { (void)http_cancel(f->http, f->request_id); }
    return result;
}
OAuthResult oauth_begin(OAuthFlow *f, bool browser)
{
    if (f == NULL || f->destroyed || f->state != OAUTH_IDLE) { return OAUTH_INVALID; }
    OAuthResult result = oauth_loopback_open(f);
    if (result != OAUTH_OK) { return oauth_terminal(f, result); }
    Crypto crypto; crypto_init(&crypto); uint8_t random[64];
    if (crypto_random(&crypto, random, sizeof(random)) != CRYPTO_OK) {
        SecureZeroMemory(random, sizeof(random)); return oauth_terminal(f, OAUTH_IO_ERROR);
    }
    base64url(random, 32, f->verifier); base64url(random + 32, 32, f->state_key);
    SecureZeroMemory(random, sizeof(random));
    result = oauth_pkce_challenge(f->verifier, f->challenge);
    if (result != OAUTH_OK) { return oauth_terminal(f, result); }
    OAuthWriter w = {f->authorization, sizeof(f->authorization), 0, false};
    oauth_write(&w, "https://accounts.spotify.com/authorize?");
    oauth_pair(&w, "response_type", "code"); oauth_pair(&w, "client_id", f->client_id);
    oauth_pair(&w, "redirect_uri", f->redirect); oauth_pair(&w, "scope", f->scopes);
    oauth_pair(&w, "state", f->state_key); oauth_pair(&w, "code_challenge_method", "S256");
    oauth_pair(&w, "code_challenge", f->challenge);
    if (w.full) { return oauth_terminal(f, OAUTH_FULL); }
    f->authorization[w.used - 1] = 0;
    f->state = OAUTH_WAITING; f->result = OAUTH_PENDING;
    f->deadline = core_deadline_after(core_clock_now(&f->clock), f->timeout_ms);
    if (browser) {
        wchar_t url[4096];
        bool opened = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, f->authorization, -1, url, 4096) != 0 &&
            (INT_PTR)ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL) > 32;
        SecureZeroMemory(url, sizeof(url));
        if (!opened) { return oauth_terminal(f, OAUTH_IO_ERROR); }
    }
    return OAUTH_OK;
}
const char *oauth_authorization_url(const OAuthFlow *f) { return f == NULL ? NULL : f->authorization; }
const char *oauth_redirect_uri(const OAuthFlow *f) { return f == NULL ? NULL : f->redirect; }
OAuthState oauth_state(const OAuthFlow *f) { return f == NULL ? OAUTH_TERMINAL : f->state; }
OAuthResult oauth_result(const OAuthFlow *f) { return f == NULL ? OAUTH_INVALID : f->result; }
OAuthResult oauth_poll(OAuthFlow *f)
{
    if (f == NULL || f->destroyed) { return OAUTH_INVALID; }
    if (core_deadline_expired(core_clock_now(&f->clock), f->deadline)) { (void)oauth_terminal(f, OAUTH_EXPIRED); }
    if (f->http_active) { return oauth_token_poll(f); }
    return f->state == OAUTH_WAITING ? oauth_loopback_poll(f) : f->result;
}
size_t oauth_wait_handles(OAuthFlow *f, HANDLE *handles, size_t cap)
{
    if (f == NULL || f->destroyed || handles == NULL) { return 0; }
    size_t n = 0;
    if (f->peer == INVALID_SOCKET && f->listen_event != WSA_INVALID_EVENT && n < cap) { handles[n++] = f->listen_event; }
    if (f->peer != INVALID_SOCKET && f->peer_event != WSA_INVALID_EVENT && n < cap) { handles[n++] = f->peer_event; }
    if (f->http_active && n < cap) { handles[n++] = http_wait_handle(f->http); }
    return n;
}
uint32_t oauth_next_wait_ms(const OAuthFlow *f)
{
    if (f == NULL || f->destroyed) { return INFINITE; }
    uint64_t deadline = f->deadline < f->peer_deadline ? f->deadline : f->peer_deadline;
    uint32_t wait = core_deadline_wait(core_clock_now(&f->clock), deadline);
    uint32_t http_wait_ms = f->http_active ? http_next_wait_ms(f->http) : INFINITE;
    return wait < http_wait_ms ? wait : http_wait_ms;
}
OAuthResult oauth_cancel(OAuthFlow *f)
{
    return f == NULL || f->destroyed ? OAUTH_INVALID : oauth_terminal(f, OAUTH_CANCELLED);
}
OAuthResult oauth_shutdown(OAuthFlow *f)
{
    if (f == NULL) { return OAUTH_INVALID; }
    if (f->destroyed) { return OAUTH_OK; }
    if (f->state != OAUTH_TERMINAL) { (void)oauth_cancel(f); }
    if (f->http_active) { (void)oauth_token_poll(f); if (f->http_active) { return OAUTH_PENDING; } }
    oauth_wipe_transient(f); f->destroyed = true;
    return oauth_balanced(f) ? OAUTH_OK : OAUTH_IO_ERROR;
}
bool oauth_balanced(const OAuthFlow *f)
{
    return f != NULL && !f->winsock && f->live_sockets == 0 && f->live_events == 0 && !f->http_active && !f->cleanup_failed;
}
bool oauth_scope_granted(const OAuthToken *token, const char *scope)
{
    if (token == NULL || scope == NULL || *scope == 0 || strchr(scope, ' ') != NULL) { return false; }
    size_t n = strlen(scope);
    for (const char *p = token->scope; *p != 0;) {
        const char *end = strchr(p, ' '); size_t bytes = end == NULL ? strlen(p) : (size_t)(end - p);
        if (bytes == n && memcmp(p, scope, n) == 0) { return true; }
        if (end == NULL) { break; } p = end + 1;
    }
    return false;
}
