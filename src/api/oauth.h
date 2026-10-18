#ifndef SPOTIC_API_OAUTH_H
#define SPOTIC_API_OAUTH_H
#include "http_client.h"
#include "protocol/credential_store.h"

typedef struct OAuthFlow OAuthFlow;
typedef enum OAuthResult { OAUTH_OK, OAUTH_PENDING, OAUTH_INVALID, OAUTH_FULL,
    OAUTH_STATE_MISMATCH, OAUTH_REPLAY, OAUTH_CANCELLED, OAUTH_EXPIRED,
    OAUTH_DENIED, OAUTH_IO_ERROR, OAUTH_HTTP_ERROR, OAUTH_BAD_RESPONSE } OAuthResult;
typedef enum OAuthState { OAUTH_IDLE, OAUTH_WAITING, OAUTH_CODE, OAUTH_HTTP,
    OAUTH_DONE, OAUTH_TERMINAL } OAuthState;
typedef struct OAuthConfig {
    const char *client_id, *redirect_uri, *scopes;
    CoreClock clock;
    uint32_t timeout_ms; /* 0: 180000, maximum 300000. */
    /* Explicit offline fixture override: only http://127.0.0.1:<port>/... . */
    const wchar_t *fixture_token_url;
} OAuthConfig;
typedef struct OAuthToken { char access[1025], scope[513]; uint64_t expires_at; } OAuthToken;

/* One network owner. Flow and all secrets occupy caller's <=256 KiB secret
 * arena, phase-shared with protocol scratch. No heap, thread or silent growth.
 * Registered redirect is IPv4 loopback; omitted port permits an ephemeral one.
 * Finish shutdown before reclaiming the arena or destroying the HTTP client. */
OAuthResult oauth_create(Arena *secret_arena, const OAuthConfig *config, OAuthFlow **output);
OAuthResult oauth_begin(OAuthFlow *flow, bool open_browser);
const char *oauth_authorization_url(const OAuthFlow *flow);
const char *oauth_redirect_uri(const OAuthFlow *flow);
OAuthState oauth_state(const OAuthFlow *flow);
OAuthResult oauth_result(const OAuthFlow *flow);
/* Begin exactly once after accepted callback, or refresh an initialized record.
 * Client ID must match; HTTP must allow Content-Type to the token origin.
 * No POST retries or redirects. Borrow client until HTTP retirement. */
OAuthResult oauth_exchange(OAuthFlow *flow, HttpClient *http);
OAuthResult oauth_refresh(OAuthFlow *flow, HttpClient *http, const CredentialRecord *record);
OAuthResult oauth_poll(OAuthFlow *flow);
size_t oauth_wait_handles(OAuthFlow *flow, HANDLE *handles, size_t capacity);
uint32_t oauth_next_wait_ms(const OAuthFlow *flow);
/* One-use successful result. Updates refresh token atomically, preserves device
 * and AP credentials. Access tokens remain memory-only; caller must wipe output. */
OAuthResult oauth_take_token(OAuthFlow *flow, CredentialRecord *record, OAuthToken *output);
bool oauth_scope_granted(const OAuthToken *token, const char *scope);
OAuthResult oauth_cancel(OAuthFlow *flow);
OAuthResult oauth_shutdown(OAuthFlow *flow); /* PENDING until cancelled HTTP retires. */
bool oauth_balanced(const OAuthFlow *flow);
OAuthResult oauth_pkce_challenge(const char *verifier, char challenge[44]);
#endif
