#include "internal.h"
#include "api/json_base.h"
#include <wchar.h>

static ApResult send_key(void *ctx, uint8_t cmd, const void *p, size_t n) { return ap_send(((Headless *)ctx)->ap, cmd, p, n); }
static void packet(void *ctx, ApSession *ap, const ApPacketView *view)
{
    Headless *h = ctx;
    if (view->command == AP_CMD_WELCOME) { h->keys_connected = audio_key_connect(h->keys, ap_generation(ap)) == AP_OK; }
    else if (view->command == AUDIO_KEY_RESPONSE || view->command == AUDIO_KEY_ERROR) { (void)ap_dispatch_audio_key(ap, h->keys); }
}
/* Exact OS identity, avoiding the manifest-dependent GetVersionEx shim. */
static bool version(uint32_t *major, uint32_t *build)
{
    typedef LONG (WINAPI *Version)(OSVERSIONINFOW *);
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    FARPROC address = module ? GetProcAddress(module, "RtlGetVersion") : NULL;
    Version fn; if (!address) { return false; }
    memcpy(&fn, &address, sizeof(fn)); OSVERSIONINFOW info = {0}; info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info)) { return false; } *major = info.dwMajorVersion; *build = info.dwBuildNumber; return true;
}
bool hl_live_poll(Headless *h)
{
    if (!h->ap) { return true; }
    uint32_t gen = ap_generation(h->ap);
    (void)ap_on_event(h->ap, (ApEvent){AP_EVENT_TIMER,gen});
    (void)ap_on_event(h->ap, (ApEvent){AP_EVENT_SOCKET,gen});
    (void)audio_key_tick(h->keys);
    if (h->tokens) { (void)token_poll(h->tokens); }
    return !h->keys_connected || ap_state(h->ap) == AP_STATE_AUTHENTICATED;
}
bool hl_live_open(Headless *h)
{
    CredentialStore store = {0};
    if (credential_store_load(&store, h->credential_path, &h->credentials) != CREDENTIAL_OK ||
        !credential_store_balanced(&store) || h->credentials.ap_bytes == 0) { return false; }
    HttpConfig hc = {.metrics = &h->accounting};
    if (http_client_create(&h->arenas[2], &hc, &h->http) != HTTP_OK) { return false; }
    /* Startup scratch is reclaimed before constructing the persistent AP. */
    Arena *scratch = &h->arenas[1];
    size_t cap = JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE;
    char *body = arena_alloc(scratch, cap, 1); void *pool = arena_alloc(scratch, 65536, 16);
    HttpRequestId id; if (!body || !pool || ap_resolve_start(h->http, body, JSON_BASE_INPUT_LIMIT, &id) != AP_OK) { return false; }
    HttpCompletion c;
    while (http_consume(h->http, id, &c) == HTTP_PENDING) { if (http_wait(h->http, 15000) != HTTP_OK) { return false; } }
    ApEndpoints endpoints;
    if (!c.final || c.result != HTTP_OK || c.status != 200 ||
        ap_resolve_parse(body, c.bytes, cap, pool, 65536, &endpoints) != AP_OK ||
        !endpoints.spclient_count || endpoints.spclient[0].port != 443) { return false; }
    if (!arena_reset(scratch) || http_client_shutdown(h->http, 15000) != HTTP_OK) { return false; }
    h->http = NULL; if (!arena_reset(&h->arenas[2])) { return false; }
    wchar_t host[AP_HOST_CHARS];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, endpoints.spclient[0].host, -1, host, AP_HOST_CHARS) ||
        swprintf_s(h->origin, 256, L"https://%ls", host) <= 0) { return false; }
    hc.header_origin_count = 3; hc.header_origins[0] = L"https://clienttoken.spotify.com";
    hc.header_origins[1] = L"https://login5.spotify.com"; hc.header_origins[2] = h->origin;
    if (http_client_create(&h->arenas[2], &hc, &h->http) != HTTP_OK) { return false; }
    AudioKeyConfig kc = {.send = send_key, .context = h};
    if (audio_key_create(scratch, &kc, &h->keys) != AP_OK || ap_auth_init(&h->auth, &h->credentials, NULL) != AP_OK) { return false; }
    ApConfig ac = {.http = h->http, .version = 124200290, .auth = &h->auth, .on_packet = packet, .context = h};
    if (ap_session_create(scratch, &ac, &h->ap) != AP_OK || ap_start(h->ap, &endpoints) != AP_OK) { return false; }
    uint64_t until = GetTickCount64() + 90000;
    while (ap_state(h->ap) != AP_STATE_AUTHENTICATED && GetTickCount64() < until) {
        HANDLE handles[5]; size_t n = ap_wait_handles(h->ap, handles, 5);
        if (!n || WaitForMultipleObjects((DWORD)n, handles, FALSE, 100) == WAIT_FAILED || !hl_live_poll(h)) { return false; }
    }
    if (!h->keys_connected || ap_auth_result(&h->auth) != AP_OK) { return false; }
    TokenConfig tc = {.http = h->http, .credentials = &h->credentials, .client_id = h->credentials.client_id,
        .client_version = "1.2.42.290", .service_origin_count = 1};
    tc.service_origins[0] = h->origin;
    if (!version(&tc.windows_major, &tc.windows_build) || token_manager_create(&h->arenas[11], &tc, &h->tokens) != TOKEN_OK) { return false; }
    MediaConfig mc = {.http = h->http, .keys = h->keys, .tokens = h->tokens, .spclient_origin = h->origin};
    memcpy(mc.policy.country, h->country, 3); mc.policy.catalogue = h->catalogue;
    return media_resolver_create(&h->arenas[3], &mc, &h->media) == MEDIA_OK;
}
bool hl_live_close(Headless *h)
{
    bool ok = true;
    if (h->tokens) {
        uint64_t until = GetTickCount64() + 15000;
        while (token_manager_shutdown(h->tokens) == TOKEN_PENDING) {
            if (GetTickCount64() >= until) { return false; } (void)http_wait(h->http, 10);
        }
        ok &= token_balanced(h->tokens);
    }
    if (h->keys) { ok &= audio_key_disconnect(h->keys, false) == AP_OK; }
    if (h->ap) {
        uint64_t until = GetTickCount64() + 15000;
        while (ap_session_shutdown(h->ap) == AP_PENDING) {
            if (GetTickCount64() >= until) { return false; }
            HANDLE events[5]; size_t n = ap_wait_handles(h->ap, events, 5);
            if (n) { (void)WaitForMultipleObjects((DWORD)n, events, FALSE, 10); }
        }
    }
    ap_auth_clear(&h->auth); credential_record_clear(&h->credentials); return ok;
}
