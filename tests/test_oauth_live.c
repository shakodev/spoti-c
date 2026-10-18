#include <winsock2.h>
#include "api/oauth.h"
#include "protocol/ap_auth.h"
#include "protocol/ap_session.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include <string.h>

static _Alignas(16) uint8_t secret_memory[MEMORY_BUDGET_PROTOCOL], http_memory[MEMORY_BUDGET_HTTP];
static bool environment(const char *name, char *value, DWORD cap)
{
    DWORD n = GetEnvironmentVariableA(name, value, cap); return n != 0 && n < cap;
}
int main(void)
{
    char client[129], redirect[256], scopes[513];
    if (!environment("SPOTIC_OAUTH_CLIENT_ID", client, sizeof(client)) ||
        !environment("SPOTIC_OAUTH_REDIRECT_URI", redirect, sizeof(redirect)) ||
        !environment("SPOTIC_OAUTH_SCOPES", scopes, sizeof(scopes))) {
        puts("SKIP: configure SPOTIC_OAUTH_CLIENT_ID, SPOTIC_OAUTH_REDIRECT_URI and SPOTIC_OAUTH_SCOPES"); return 77;
    }
    Arena secret, transport;
    CHECK(arena_init(&secret, secret_memory, sizeof(secret_memory)) && arena_init(&transport, http_memory, sizeof(http_memory)));
    HttpConfig hc = {.header_origin_count = 1}; hc.header_origins[0] = L"https://accounts.spotify.com";
    HttpClient *http; CHECK(http_client_create(&transport, &hc, &http) == HTTP_OK);
    OAuthConfig oc = {.client_id = client, .redirect_uri = redirect, .scopes = scopes}; OAuthFlow *flow;
    CHECK(oauth_create(&secret, &oc, &flow) == OAUTH_OK && oauth_begin(flow, true) == OAUTH_OK);
    puts("Complete login in the system browser. No tokens or account identifiers will be printed.");
    while (oauth_state(flow) != OAUTH_DONE && oauth_state(flow) != OAUTH_TERMINAL) {
        if (oauth_state(flow) == OAUTH_CODE) { CHECK(oauth_exchange(flow, http) == OAUTH_OK); }
        HANDLE handles[3]; size_t n = oauth_wait_handles(flow, handles, 3);
        uint32_t wait = oauth_next_wait_ms(flow);
        CHECK(n != 0 && WaitForMultipleObjects((DWORD)n, handles, FALSE, wait) != WAIT_FAILED);
        (void)oauth_poll(flow);
    }
    CredentialRecord record; OAuthToken token = {0}; CHECK(credential_record_init(&record, client) == CREDENTIAL_OK);
    bool success = oauth_result(flow) == OAUTH_OK && oauth_take_token(flow, &record, &token) == OAUTH_OK;
    while (oauth_shutdown(flow) == OAUTH_PENDING) { CHECK(http_wait(http, 1000) == HTTP_OK); }
    CHECK(oauth_balanced(flow)); CHECK(arena_reset(&secret));
    if (success) {
        /* This actual encrypted AP login, including its stored-credential
         * reconnect, is the compatibility probe. OAuth issuance is insufficient. */
        ApAuth auth; CHECK(ap_auth_init(&auth, &record, token.access) == AP_OK);
        SecureZeroMemory(&token, sizeof(token));
        ApConfig ac = {.http = http, .version = 124200290, .auth = &auth}; ApSession *session;
        CHECK(ap_session_create(&secret, &ac, &session) == AP_OK && ap_start(session, NULL) == AP_OK);
        uint64_t until = GetTickCount64() + 90000;
        while (ap_state(session) != AP_STATE_AUTHENTICATED && ap_state(session) != AP_STATE_CLOSED && GetTickCount64() < until) {
            HANDLE handles[4]; size_t n = ap_wait_handles(session, handles, 4);
            uint32_t wait = ap_next_wait_ms(session); if (wait > 10000) { wait = 10000; }
            DWORD woke = n == 0 ? (Sleep(wait), WAIT_TIMEOUT) : WaitForMultipleObjects((DWORD)n, handles, FALSE, wait);
            CHECK(woke != WAIT_FAILED); uint32_t generation = ap_generation(session);
            (void)ap_on_event(session, (ApEvent){AP_EVENT_TIMER, generation});
            if (ap_state(session) == AP_STATE_RESOLVE) { (void)ap_on_event(session, (ApEvent){AP_EVENT_HTTP, generation}); }
            else { (void)ap_on_event(session, (ApEvent){AP_EVENT_SOCKET, generation}); }
        }
        success = ap_state(session) == AP_STATE_AUTHENTICATED && ap_auth_result(&auth) == AP_OK && record.ap_bytes != 0;
        while (ap_session_shutdown(session) == AP_PENDING) {
            HANDLE handles[4]; size_t n = ap_wait_handles(session, handles, 4);
            if (n != 0) { CHECK(WaitForMultipleObjects((DWORD)n, handles, FALSE, 1000) != WAIT_FAILED); }
        }
        ap_auth_clear(&auth);
    }
    CHECK(http_client_shutdown(http, 5000) == HTTP_OK);
    credential_record_clear(&record); SecureZeroMemory(&token, sizeof(token)); arena_dispose(&secret); arena_dispose(&transport);
    puts(success ? "Live OAuth and stored-credential AP reconnect accepted." : "Live OAuth/AP compatibility failed; token equivalence is unproven.");
    return success ? 0 : 1;
}
