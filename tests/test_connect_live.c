#include "protocol/dealer.h"
#include "headless/internal.h"
#include "api/json_base.h"
#include "support/test_check.h"

/* Explicit account/two-device acceptance harness. Uses the production Dealer
 * and state HTTP paths, Player and headless media owners; no synthetic auth. */
static _Alignas(16) unsigned char core_memory[128 * 1024], protocol_memory[256 * 1024], http_memory[384 * 1024], secrets[TOKEN_SECRET_LIMIT];
static CredentialRecord credentials;
static bool windows_version(uint32_t *major, uint32_t *build)
{
    typedef LONG (WINAPI *Version)(OSVERSIONINFOW *);
    FARPROC address = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); Version fn;
    if (!address) { return false; } memcpy(&fn, &address, sizeof(fn));
    OSVERSIONINFOW info = {0}; info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info)) { return false; } *major = info.dwMajorVersion; *build = info.dwBuildNumber; return true;
}
int main(void)
{
    wchar_t path[240], cache[240]; char country[3], catalogue[33];
    DWORD a = GetEnvironmentVariableW(L"SPOTIC_HEADLESS_CREDENTIALS", path, 240), b = GetEnvironmentVariableW(L"SPOTIC_HEADLESS_CACHE", cache, 240);
    DWORD c = GetEnvironmentVariableA("SPOTIC_MEDIA_COUNTRY", country, 3), e = GetEnvironmentVariableA("SPOTIC_MEDIA_CATALOGUE", catalogue, 33);
    if (!a || a >= 240 || !b || b >= 240 || c != 2 || !e || e >= 33) {
        puts("SKIP: set private SPOTIC_HEADLESS_CREDENTIALS/CACHE and SPOTIC_MEDIA_COUNTRY/CATALOGUE; use a second signed-in device."); return 77;
    }
    CredentialStore store = {0}; CHECK(credential_store_load(&store, path, &credentials) == CREDENTIAL_OK && credential_store_balanced(&store));
    Arena core, protocol, transport, secret;
    CHECK(arena_init(&core, core_memory, sizeof(core_memory)) && arena_init(&protocol, protocol_memory, sizeof(protocol_memory)));
    CHECK(arena_init(&transport, http_memory, sizeof(http_memory)) && arena_init(&secret, secrets, sizeof(secrets)));
    HttpClient *http; HttpConfig hc = {0}; CHECK(http_client_create(&transport, &hc, &http) == HTTP_OK);
    char *body = arena_alloc(&protocol, JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE, 16);
    void *pool = arena_alloc(&protocol, 65536, 16); HttpRequestId id;
    CHECK(body && pool && ap_resolve_start(http, body, JSON_BASE_INPUT_LIMIT, &id) == AP_OK);
    HttpCompletion completion; while (http_consume(http, id, &completion) == HTTP_PENDING) { CHECK(http_wait(http, 15000) == HTTP_OK); }
    ApEndpoints endpoints;
    CHECK(completion.result == HTTP_OK && ap_resolve_parse(body, completion.bytes, JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE, pool, 65536, &endpoints) == AP_OK);
    CHECK(endpoints.dealer_count && endpoints.spclient_count && endpoints.spclient[0].port == 443);
    CHECK(http_client_shutdown(http, 15000) == HTTP_OK && arena_reset(&protocol) && arena_reset(&transport));
    wchar_t spclient[300], dealer_origin[300];
    CHECK(swprintf_s(spclient, 300, L"https://%S", endpoints.spclient[0].host) > 0);
    CHECK(swprintf_s(dealer_origin, 300, L"https://%S", endpoints.dealer[0].host) > 0);
    hc.header_origin_count = 3; hc.header_origins[0] = L"https://clienttoken.spotify.com"; hc.header_origins[1] = L"https://login5.spotify.com"; hc.header_origins[2] = spclient;
    CHECK(http_client_create(&transport, &hc, &http) == HTTP_OK);
    TokenConfig tc = {.http = http, .credentials = &credentials, .client_id = credentials.client_id, .client_version = "1.2.42.290", .service_origin_count = 2};
    tc.service_origins[0] = spclient; tc.service_origins[1] = dealer_origin; CHECK(windows_version(&tc.windows_major, &tc.windows_build));
    TokenManager *tokens; CHECK(token_manager_create(&secret, &tc, &tokens) == TOKEN_OK);
    HeadlessConfig pc = {.credential_path = path, .cache_directory = cache, .country = country, .catalogue = catalogue, .wasapi = true, .realtime = true};
    Headless *player; CHECK(headless_open(&pc, &player));
    ConnectConfig cfg = {.player = player->player, .http = http, .tokens = tokens, .device_id = credentials.device_id,
        .name = "spoti-c Connect acceptance", .client_id = credentials.client_id, .spclient_origin = spclient};
    ConnectState *state; CHECK(connect_create(&core, &cfg, &state) == DEALER_OK);
    DealerConfig dc = {.connect = state, .tokens = tokens, .endpoint = endpoints.dealer[0]}; DealerClient *dealer;
    CHECK(dealer_create(&core, &protocol, &dc, &dealer) == DEALER_OK);
    puts("Within five minutes select 'spoti-c Connect acceptance' on a second device, transfer a finite track queue, then play/pause, seek, next/previous and change volume.");
    uint64_t until = GetTickCount64() + 300000; uint32_t controls = 0; bool played = false, logged_out = false;
    while (GetTickCount64() < until && (controls != 127 || !played)) {
        (void)dealer_pump(dealer); ConnectSnapshot s = connect_snapshot(state); controls = s.controls;
        PlayerSnapshot p = headless_snapshot(player); played |= p.state == PLAYER_PLAYING && p.position_frames > p.rate;
        logged_out = s.logged_out; if (p.state == PLAYER_ERROR || s.logged_out) { break; }
        HANDLE handles[3]; DWORD count = (DWORD)dealer_wait_handles(dealer, handles);
        DWORD wait = dealer_next_wait_ms(dealer), http_wait_ms = http_next_wait_ms(http);
        if (wait > http_wait_ms) { wait = http_wait_ms; } if (wait > 1000) { wait = 1000; }
        CHECK(WaitForMultipleObjects(count, handles, FALSE, wait) != WAIT_FAILED);
    }
    until = GetTickCount64() + 15000;
    while (dealer_shutdown(dealer) != DEALER_CLOSED || connect_shutdown(state) == DEALER_PENDING) { CHECK(GetTickCount64() < until); (void)http_wait(http, 10); }
    while (token_manager_shutdown(tokens) == TOKEN_PENDING) { CHECK(GetTickCount64() < until); (void)http_wait(http, 10); }
    CHECK(token_balanced(tokens) && http_client_shutdown(http, 15000) == HTTP_OK);
    HeadlessMetrics metrics; CHECK(headless_close(player, &metrics) && metrics.balanced);
    if (logged_out) { CHECK(credential_store_logout(&store, path, &credentials) == CREDENTIAL_OK && credential_store_balanced(&store)); }
    credential_record_clear(&credentials); arena_dispose(&secret); arena_dispose(&protocol); arena_dispose(&transport); arena_dispose(&core);
    SecureZeroMemory(secrets, sizeof(secrets)); CHECK(controls == 127 && played);
    puts("Two-device discovery/control and audible playback timeline verified; all owners retired."); return 0;
}
