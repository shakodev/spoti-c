#include <winsock2.h>
#include "api/oauth.h"
#include "api/json_base.h"
#include "protocol/ap_auth.h"
#include "protocol/ap_session.h"
#include "protocol/media_source.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include <string.h>
#include <wchar.h>

/* Built only by ENABLE_LIVE_MEDIA_TEST. No credentials, keys, track IDs or
 * signed URLs are logged or persisted by this probe. */
static _Alignas(16) uint8_t protocol_memory[MEMORY_BUDGET_PROTOCOL];
static _Alignas(16) uint8_t http_memory[MEMORY_BUDGET_HTTP];
static _Alignas(16) uint8_t token_memory[TOKEN_SECRET_LIMIT];
static _Alignas(16) uint8_t media_memory[MEMORY_BUDGET_METADATA];
typedef struct Live {
    HttpClient *http;
    ApSession *ap;
    AudioKeyClient *keys;
    TokenManager *tokens;
    MediaResolver *media;
    bool keys_connected, completed, transport_failed;
    MediaResult result;
} Live;

static bool environment(const char *name, char *value, DWORD capacity)
{
    DWORD n = GetEnvironmentVariableA(name, value, capacity); return n != 0 && n < capacity;
}

static uint64_t utc_ms(void *context)
{
    (void)context; FILETIME file; GetSystemTimeAsFileTime(&file);
    uint64_t ticks = ((uint64_t)file.dwHighDateTime << 32) | file.dwLowDateTime;
    return ticks / 10000 - UINT64_C(11644473600000);
}

static void windows_version(uint32_t *major, uint32_t *build)
{
    typedef LONG (WINAPI *VersionFunction)(OSVERSIONINFOW *);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"); CHECK(ntdll != NULL);
    FARPROC address = GetProcAddress(ntdll, "RtlGetVersion"); CHECK(address != NULL);
    VersionFunction version;
    _Static_assert(sizeof(version) == sizeof(address), "Windows function pointer representation");
    memcpy(&version, &address, sizeof(version));
    OSVERSIONINFOW info = {0}; info.dwOSVersionInfoSize = sizeof(info);
    CHECK(version(&info) == 0); *major = info.dwMajorVersion; *build = info.dwBuildNumber;
}

static ApResult send_key(void *context, uint8_t command, const void *data, size_t bytes)
{
    return ap_send(((Live *)context)->ap, command, data, bytes);
}

static void on_packet(void *context, ApSession *session, const ApPacketView *view)
{
    Live *live = context;
    if (view->command == AP_CMD_WELCOME) {
        CHECK(audio_key_connect(live->keys, ap_generation(session)) == AP_OK); live->keys_connected = true;
    } else if (view->command == AUDIO_KEY_RESPONSE || view->command == AUDIO_KEY_ERROR) {
        (void)ap_dispatch_audio_key(session, live->keys);
    }
}

static void resolved(void *context, MediaId id, MediaResult result)
{
    (void)id; Live *live = context; CHECK(!live->completed);
    live->completed = true; live->result = result;
}

static uint32_t minimum(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* All modules share one network owner, one HTTP client and the actual nearest
 * deadline. The only zero-time wake is bounded work requested by a module. */
static void drive(Live *live, uint64_t until)
{
    HANDLE handles[6]; size_t n = ap_wait_handles(live->ap, handles, 5);
    HANDLE http_event = http_wait_handle(live->http); bool duplicate = false;
    for (size_t i = 0; i < n; ++i) { duplicate = duplicate || handles[i] == http_event; }
    if (!duplicate) { handles[n++] = http_event; }
    uint32_t wait = core_deadline_wait(GetTickCount64(), until);
    wait = minimum(wait, ap_next_wait_ms(live->ap));
    wait = minimum(wait, http_next_wait_ms(live->http));
    wait = minimum(wait, audio_key_next_wait_ms(live->keys));
    if (live->tokens != NULL) { wait = minimum(wait, token_next_wait_ms(live->tokens)); }
    if (live->media != NULL) { wait = minimum(wait, media_source_next_wait_ms(live->media)); }
    CHECK(WaitForMultipleObjects((DWORD)n, handles, FALSE, wait) != WAIT_FAILED);
    uint32_t generation = ap_generation(live->ap);
    (void)ap_on_event(live->ap, (ApEvent){AP_EVENT_TIMER, generation});
    (void)ap_on_event(live->ap, (ApEvent){AP_EVENT_SOCKET, generation});
    CHECK(http_pump(live->http) == HTTP_OK);
    if (live->keys_connected && ap_state(live->ap) != AP_STATE_AUTHENTICATED) {
        CHECK(audio_key_disconnect(live->keys, true) == AP_OK);
        live->keys_connected = false; live->transport_failed = true;
    }
    CHECK(audio_key_tick(live->keys) == AP_OK);
    if (live->tokens != NULL) { (void)token_poll(live->tokens); }
    if (live->media != NULL) { (void)media_source_poll(live->media); }
}

static void resolve_endpoints(Arena *scratch, HttpClient *http, ApEndpoints *endpoints)
{
    size_t cap = JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE;
    char *body = arena_alloc(scratch, cap, 1);
    void *pool = arena_alloc(scratch, 65536, 16); CHECK(body != NULL && pool != NULL);
    HttpRequestId id; CHECK(ap_resolve_start(http, body, JSON_BASE_INPUT_LIMIT, &id) == AP_OK);
    HttpCompletion completion;
    for (;;) {
        HttpResult result = http_consume(http, id, &completion);
        if (result == HTTP_OK) { break; }
        CHECK(result == HTTP_PENDING && http_wait(http, INFINITE) == HTTP_OK);
    }
    CHECK(completion.final && completion.result == HTTP_OK && completion.status == 200);
    CHECK(ap_resolve_parse(body, completion.bytes, cap, pool, 65536, endpoints) == AP_OK);
    CHECK(endpoints->spclient_count != 0 && endpoints->spclient[0].port == 443);
    CHECK(arena_reset(scratch));
}

int main(void)
{
    char client[129], redirect[256], scopes[513], track[MEDIA_URI_BYTES], country[3], catalogue[65];
    if (!environment("SPOTIC_OAUTH_CLIENT_ID", client, sizeof(client)) ||
        !environment("SPOTIC_OAUTH_REDIRECT_URI", redirect, sizeof(redirect)) ||
        !environment("SPOTIC_OAUTH_SCOPES", scopes, sizeof(scopes)) ||
        !environment("SPOTIC_MEDIA_TRACK", track, sizeof(track)) ||
        !environment("SPOTIC_MEDIA_COUNTRY", country, sizeof(country)) ||
        !environment("SPOTIC_MEDIA_CATALOGUE", catalogue, sizeof(catalogue))) {
        puts("SKIP: configure OAuth client/redirect/scopes, SPOTIC_MEDIA_TRACK, SPOTIC_MEDIA_COUNTRY and SPOTIC_MEDIA_CATALOGUE"); return 77;
    }
    uint8_t parsed_gid[16]; CHECK(media_gid_from_uri(track, parsed_gid) == MEDIA_OK);
    CHECK(strlen(country) == 2 && country[0] >= 'A' && country[0] <= 'Z' && country[1] >= 'A' && country[1] <= 'Z');
    Arena protocol, transport, token_arena, media_arena;
    CHECK(arena_init(&protocol, protocol_memory, sizeof(protocol_memory)));
    CHECK(arena_init(&transport, http_memory, sizeof(http_memory)));
    CHECK(arena_init(&token_arena, token_memory, sizeof(token_memory)));
    CHECK(arena_init(&media_arena, media_memory, sizeof(media_memory)));
    HttpConfig initial_http = {.header_origin_count = 1}; initial_http.header_origins[0] = L"https://accounts.spotify.com";
    Live live = {0}; CHECK(http_client_create(&transport, &initial_http, &live.http) == HTTP_OK);
    OAuthConfig oauth_config = {.client_id = client, .redirect_uri = redirect, .scopes = scopes};
    OAuthFlow *flow; CHECK(oauth_create(&protocol, &oauth_config, &flow) == OAUTH_OK);
    CHECK(oauth_begin(flow, true) == OAUTH_OK);
    puts("Complete login in the system browser for the live media resolution probe.");
    while (oauth_state(flow) != OAUTH_DONE && oauth_state(flow) != OAUTH_TERMINAL) {
        if (oauth_state(flow) == OAUTH_CODE) { CHECK(oauth_exchange(flow, live.http) == OAUTH_OK); }
        HANDLE handles[3]; size_t n = oauth_wait_handles(flow, handles, 3);
        CHECK(n != 0 && WaitForMultipleObjects((DWORD)n, handles, FALSE, oauth_next_wait_ms(flow)) != WAIT_FAILED);
        (void)oauth_poll(flow);
    }
    CredentialRecord credentials; OAuthToken token = {0};
    CHECK(credential_record_init(&credentials, client) == CREDENTIAL_OK);
    CHECK(oauth_result(flow) == OAUTH_OK && oauth_take_token(flow, &credentials, &token) == OAUTH_OK);
    while (oauth_shutdown(flow) == OAUTH_PENDING) { CHECK(http_wait(live.http, 1000) == HTTP_OK); }
    CHECK(oauth_balanced(flow) && arena_reset(&protocol));
    ApEndpoints endpoints; resolve_endpoints(&protocol, live.http, &endpoints);
    CHECK(http_client_shutdown(live.http, 5000) == HTTP_OK && arena_reset(&transport));
    wchar_t origin[AP_HOST_CHARS + 9], host[AP_HOST_CHARS];
    CHECK(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, endpoints.spclient[0].host, -1, host, AP_HOST_CHARS) != 0);
    CHECK(swprintf_s(origin, sizeof(origin) / sizeof(origin[0]), L"https://%ls", host) > 0);
    HttpConfig http_config = {.header_origin_count = 3};
    http_config.header_origins[0] = L"https://clienttoken.spotify.com";
    http_config.header_origins[1] = L"https://login5.spotify.com"; http_config.header_origins[2] = origin;
    CHECK(http_client_create(&transport, &http_config, &live.http) == HTTP_OK);
    AudioKeyConfig key_config = {.send = send_key, .context = &live};
    CHECK(audio_key_create(&protocol, &key_config, &live.keys) == AP_OK);
    ApAuth auth; CHECK(ap_auth_init(&auth, &credentials, token.access) == AP_OK); SecureZeroMemory(&token, sizeof(token));
    ApConfig ap_config = {.http = live.http, .version = 124200290, .auth = &auth, .on_packet = on_packet, .context = &live};
    CHECK(ap_session_create(&protocol, &ap_config, &live.ap) == AP_OK && ap_start(live.ap, &endpoints) == AP_OK);
    uint64_t until = core_deadline_after(GetTickCount64(), 90000);
    while (ap_state(live.ap) != AP_STATE_AUTHENTICATED && ap_state(live.ap) != AP_STATE_CLOSED && GetTickCount64() < until) { drive(&live, until); }
    CHECK(ap_state(live.ap) == AP_STATE_AUTHENTICATED && ap_auth_result(&auth) == AP_OK && live.keys_connected);
    TokenConfig token_config = {.http = live.http, .credentials = &credentials, .client_id = client,
        .client_version = "1.2.42.290", .service_origin_count = 1};
    windows_version(&token_config.windows_major, &token_config.windows_build); token_config.service_origins[0] = origin;
    CHECK(token_manager_create(&token_arena, &token_config, &live.tokens) == TOKEN_OK);
    MediaConfig media_config = {.http = live.http, .tokens = live.tokens, .keys = live.keys,
        .spclient_origin = origin, .utc_ms = utc_ms};
    memcpy(media_config.policy.country, country, sizeof(country)); media_config.policy.catalogue = catalogue;
    CHECK(media_resolver_create(&media_arena, &media_config, &live.media) == MEDIA_OK);
    MediaId id; CHECK(media_source_resolve(live.media, track, resolved, &live, &id) == MEDIA_OK);
    until = core_deadline_after(GetTickCount64(), 90000);
    while (!live.completed && !live.transport_failed && GetTickCount64() < until) { drive(&live, until); }
    bool success = live.completed && live.result == MEDIA_OK && !live.transport_failed;
    if (success) {
        MediaSource source; CHECK(media_source_get(live.media, id, &source) == MEDIA_OK);
        CHECK(source.url_count != 0 && source.urls[0].url[0] != '\0' && source.track.duration_ms != 0);
        MediaKeyLease lease = {0}; CHECK(media_key_acquire(live.media, source.key, &lease) == MEDIA_OK && lease.key != NULL);
        CHECK(media_source_retire(live.media, id) == MEDIA_OK);
        CHECK(media_key_release(live.media, &lease) == MEDIA_OK && lease.key == NULL);
        SecureZeroMemory(&source, sizeof(source));
    }
    until = core_deadline_after(GetTickCount64(), 15000);
    while (media_resolver_shutdown(live.media) == MEDIA_PENDING) { CHECK(GetTickCount64() < until); drive(&live, until); }
    CHECK(media_resolver_balanced(live.media)); live.media = NULL;
    CHECK(audio_key_disconnect(live.keys, false) == AP_OK);
    while (token_manager_shutdown(live.tokens) == TOKEN_PENDING) { CHECK(http_wait(live.http, 1000) == HTTP_OK); }
    CHECK(token_balanced(live.tokens)); live.tokens = NULL;
    while (ap_session_shutdown(live.ap) == AP_PENDING) {
        HANDLE handles[4]; size_t n = ap_wait_handles(live.ap, handles, 4);
        CHECK(n != 0 && WaitForMultipleObjects((DWORD)n, handles, FALSE, 1000) != WAIT_FAILED);
    }
    ap_auth_clear(&auth); CHECK(http_client_shutdown(live.http, 5000) == HTTP_OK);
    credential_record_clear(&credentials); SecureZeroMemory(protocol_memory, protocol.used);
    arena_dispose(&media_arena); arena_dispose(&token_arena); arena_dispose(&protocol); arena_dispose(&transport);
    puts(success ? "Live authenticated metadata, AudioKey, CDN resolution and key lease passed." : "Live media resolution failed.");
    return success ? 0 : 1;
}
