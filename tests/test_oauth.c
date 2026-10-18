#include "oauth_test.h"
#include "core/memory_budget.h"

static _Alignas(OAuthFlow) uint8_t memory[MEMORY_BUDGET_PROTOCOL];
static Arena arena;
uint64_t oauth_test_now;
static uint64_t now(void *context) { return *(uint64_t *)context; }
bool oauth_test_zero(const void *data, size_t bytes)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; ++i) { if (p[i] != 0) { return false; } }
    return true;
}
OAuthFlow *oauth_test_open(const wchar_t *fixture)
{
    CHECK(arena_init(&arena, memory, sizeof(memory))); oauth_test_now = 1000;
    OAuthConfig config = {.client_id = "fixture-client", .redirect_uri = "http://127.0.0.1/callback",
        .scopes = "streaming user-read-private", .clock = {now, &oauth_test_now}, .fixture_token_url = fixture};
    OAuthFlow *f;
    CHECK(oauth_create(&arena, &config, &f) == OAUTH_OK);
    CHECK(arena.used == sizeof(OAuthFlow) && oauth_next_wait_ms(f) == INFINITE && oauth_balanced(f)); return f;
}
void oauth_test_close(OAuthFlow *f)
{
    uint64_t until = GetTickCount64() + 5000;
    while (oauth_shutdown(f) == OAUTH_PENDING) {
        HANDLE handles[3]; size_t n = oauth_wait_handles(f, handles, 3);
        CHECK(n != 0 && GetTickCount64() < until);
        CHECK(WaitForMultipleObjects((DWORD)n, handles, FALSE, 1000) != WAIT_FAILED);
    }
    CHECK(oauth_shutdown(f) == OAUTH_OK && oauth_balanced(f));
    CHECK(oauth_test_zero(f->verifier, sizeof(f->verifier)) && oauth_test_zero(f->refresh, sizeof(f->refresh)) &&
        oauth_test_zero(f->upload, sizeof(f->upload)) && oauth_test_zero(f->response, sizeof(f->response)) &&
        oauth_test_zero(f->json_pool, sizeof(f->json_pool)) && oauth_test_zero(&f->token, sizeof(f->token)));
    arena_dispose(&arena);
}
OAuthResult oauth_test_callback(OAuthFlow *f, const char *query)
{
    char request[4097];
    int bytes = sprintf_s(request, sizeof(request), "GET /callback?%s HTTP/1.1\r\nHost: %s\r\n\r\n", query, f->host);
    CHECK(bytes > 0); return oauth_callback_request(f, request, (size_t)bytes);
}
void oauth_test_code(OAuthFlow *f)
{
    CHECK(oauth_begin(f, false) == OAUTH_OK);
    char query[128]; CHECK(sprintf_s(query, sizeof(query), "code=auth%%2Bcode&state=%s", f->state_key) > 0);
    CHECK(oauth_test_callback(f, query) == OAUTH_OK && oauth_state(f) == OAUTH_CODE);
    CHECK(strcmp(f->code, "auth+code") == 0 && oauth_balanced(f));
}
static void pkce_and_config(void)
{
    char challenge[44];
    CHECK(oauth_pkce_challenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk", challenge) == OAUTH_OK);
    CHECK(strcmp(challenge, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM") == 0);
    CHECK(oauth_pkce_challenge("short", challenge) == OAUTH_INVALID && challenge[0] == 0);
    char bad[129]; memset(bad, 'a', 128); bad[128] = 0;
    CHECK(oauth_pkce_challenge(bad, challenge) == OAUTH_OK);
    bad[10] = '+'; CHECK(oauth_pkce_challenge(bad, challenge) == OAUTH_INVALID);
    const char *invalid[] = {"http://localhost:8080/callback", "https://127.0.0.1/callback", "http://0.0.0.0:8080/callback",
        "http://127.0.0.1.evil/callback", "http://127.0.0.1:65536/callback", "http://127.0.0.1:0/callback",
        "http://127.0.0.1:1/callback?x=y", "http://127.0.0.1:1/callback#fragment", "http://127.0.0.1:1/a/../callback"};
    CHECK(arena_init(&arena, memory, sizeof(memory)));
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        OAuthConfig config = {.client_id = "fixture", .redirect_uri = invalid[i]}; OAuthFlow *f = (OAuthFlow *)1;
        CHECK(oauth_create(&arena, &config, &f) == OAUTH_INVALID && f == NULL && arena.used == 0);
    }
    OAuthConfig c = {.client_id = "fixture", .redirect_uri = "http://127.0.0.1/callback", .fixture_token_url = L"https://evil.invalid/token"};
    OAuthFlow *f; CHECK(oauth_create(&arena, &c, &f) == OAUTH_INVALID && arena.used == 0);
    arena_dispose(&arena);
    uint8_t tiny[8]; CHECK(arena_init(&arena, tiny, sizeof(tiny))); c.fixture_token_url = NULL;
    CHECK(oauth_create(&arena, &c, &f) == OAUTH_FULL && arena.used == 0); arena_dispose(&arena);
}
static void callback_validation(void)
{
    OAuthFlow *f = oauth_test_open(NULL); CHECK(oauth_begin(f, false) == OAUTH_OK);
    CHECK(strstr(oauth_authorization_url(f), "code_challenge_method=S256") != NULL &&
        strstr(oauth_authorization_url(f), f->verifier) == NULL && strcmp(f->verifier, f->state_key) != 0);
    CHECK(strstr(oauth_authorization_url(f), "scope=streaming%20user-read-private") != NULL);
    CHECK(oauth_next_wait_ms(f) == 180000 && strstr(oauth_redirect_uri(f), f->host) != NULL);
    CHECK(oauth_test_callback(f, "code=x") == OAUTH_STATE_MISMATCH);
    CHECK(oauth_test_callback(f, "code=x&state=wrong") == OAUTH_STATE_MISMATCH);
    char query[512];
    CHECK(sprintf_s(query, sizeof(query), "code=x&state=%s&st%%61te=%s", f->state_key, f->state_key) > 0);
    CHECK(oauth_test_callback(f, query) == OAUTH_INVALID);
    const char *bad[] = {"code=%00", "code=%0d%0a", "code=%zz", "code=%", "code=x&code=y", "code=", "code=x&error=denied", "unknown=x"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(sprintf_s(query, sizeof(query), "%s&state=%s", bad[i], f->state_key) > 0);
        CHECK(oauth_test_callback(f, query) == OAUTH_INVALID && oauth_state(f) == OAUTH_WAITING);
    }
    char request[512];
    int n = sprintf_s(request, sizeof(request), "GET /wrong?code=x&state=%s HTTP/1.1\r\nHost: %s\r\n\r\n", f->state_key, f->host);
    CHECK(n > 0 && oauth_callback_request(f, request, (size_t)n) == OAUTH_INVALID);
    n = sprintf_s(request, sizeof(request), "GET /callback?code=x&state=%s HTTP/1.1\r\nHost: evil.invalid\r\n\r\n", f->state_key);
    CHECK(n > 0 && oauth_callback_request(f, request, (size_t)n) == OAUTH_INVALID);
    n = sprintf_s(request, sizeof(request), "GET /callback?code=x&state=%s HTTP/1.1\r\nHost: %s\r\nContent-Length: 1\r\n\r\nx", f->state_key, f->host);
    CHECK(n > 0 && oauth_callback_request(f, request, (size_t)n) == OAUTH_INVALID);
    CHECK(sprintf_s(query, sizeof(query), "code=a+b%%2Bc&state=%s", f->state_key) > 0);
    CHECK(oauth_test_callback(f, query) == OAUTH_OK && strcmp(f->code, "a b+c") == 0);
    CHECK(oauth_test_callback(f, query) == OAUTH_REPLAY && oauth_balanced(f)); oauth_test_close(f);
    f = oauth_test_open(NULL); CHECK(oauth_begin(f, false) == OAUTH_OK);
    CHECK(sprintf_s(query, sizeof(query), "error=access_denied&state=%s", f->state_key) > 0);
    CHECK(oauth_test_callback(f, query) == OAUTH_DENIED && oauth_state(f) == OAUTH_TERMINAL); oauth_test_close(f);
}
static SOCKET connect_peer(OAuthFlow *f)
{
    WSADATA data; CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); CHECK(peer != INVALID_SOCKET);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(f->port);
    CHECK(connect(peer, (const struct sockaddr *)&address, sizeof(address)) == 0); return peer;
}
static void loopback_and_deadlines(void)
{
    OAuthFlow *f = oauth_test_open(NULL); CHECK(oauth_begin(f, false) == OAUTH_OK);
    SOCKET peer = connect_peer(f);
    CHECK(send(peer, "GET ", 4, 0) == 4); CHECK(oauth_poll(f) == OAUTH_PENDING);
    CHECK(f->peer != INVALID_SOCKET && oauth_next_wait_ms(f) == 5000);
    oauth_test_now += 5000; CHECK(oauth_poll(f) == OAUTH_PENDING && f->peer == INVALID_SOCKET);
    CHECK(closesocket(peer) == 0 && WSACleanup() == 0);
    peer = connect_peer(f); char request[512];
    int n = sprintf_s(request, sizeof(request), "GET /callback?code=loopback&state=%s HTTP/1.1\r\nHost: %s\r\n\r\n", f->state_key, f->host);
    CHECK(n > 0 && send(peer, request, 9, 0) == 9 && oauth_poll(f) == OAUTH_PENDING);
    CHECK(send(peer, request + 9, n - 9, 0) == n - 9);
    uint64_t until = GetTickCount64() + 3000;
    while (oauth_state(f) == OAUTH_WAITING) {
        HANDLE handles[3]; size_t count = oauth_wait_handles(f, handles, 3); CHECK(count == 1);
        CHECK(WaitForMultipleObjects((DWORD)count, handles, FALSE, 1000) != WAIT_FAILED && GetTickCount64() < until);
        CHECK(oauth_poll(f) == OAUTH_PENDING || oauth_state(f) == OAUTH_CODE);
    }
    CHECK(strcmp(f->code, "loopback") == 0); CHECK(closesocket(peer) == 0 && WSACleanup() == 0); oauth_test_close(f);
    f = oauth_test_open(NULL); CHECK(oauth_begin(f, false) == OAUTH_OK); oauth_test_now += 180000;
    CHECK(oauth_poll(f) == OAUTH_EXPIRED && oauth_balanced(f) && oauth_next_wait_ms(f) == INFINITE); oauth_test_close(f);
    f = oauth_test_open(NULL); CHECK(oauth_begin(f, false) == OAUTH_OK);
    CHECK(oauth_cancel(f) == OAUTH_CANCELLED && oauth_balanced(f)); oauth_test_close(f);
    f = oauth_test_open(NULL); CHECK(oauth_begin(f, false) == OAUTH_OK); peer = connect_peer(f);
    char oversized[4096]; memset(oversized, 'x', sizeof(oversized));
    CHECK(send(peer, oversized, sizeof(oversized), 0) == sizeof(oversized));
    CHECK(oauth_poll(f) == OAUTH_PENDING && f->peer == INVALID_SOCKET && f->request_bytes == 0);
    CHECK(closesocket(peer) == 0 && WSACleanup() == 0); oauth_test_close(f);
}
int main(void)
{
    pkce_and_config(); callback_validation(); loopback_and_deadlines(); test_oauth_http();
    puts("OAuth PKCE, state/redirect/loopback deadlines, token exchange and rotation passed"); return 0;
}
