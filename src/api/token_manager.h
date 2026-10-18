#ifndef SPOTIC_API_TOKEN_MANAGER_H
#define SPOTIC_API_TOKEN_MANAGER_H
#include "http_client.h"
#include "protocol/credential_store.h"

#define TOKEN_SECRET_LIMIT ((size_t)32 * 1024)
#define TOKEN_ORIGIN_COUNT 8u
#define TOKEN_TEXT_CAP 1024u
typedef struct TokenManager TokenManager;
typedef enum TokenKind { TOKEN_CLIENT, TOKEN_BEARER } TokenKind;
typedef enum TokenResult { TOKEN_OK, TOKEN_PENDING, TOKEN_INVALID, TOKEN_FULL,
    TOKEN_CLIENT_MISMATCH, TOKEN_SCOPE, TOKEN_UNSUPPORTED, TOKEN_CHALLENGE_LIMIT,
    TOKEN_TIMEOUT, TOKEN_CLOCK_CHANGED, TOKEN_HTTP_ERROR, TOKEN_REJECTED,
    TOKEN_MALFORMED, TOKEN_CANCELLED, TOKEN_CLOSED, TOKEN_EXHAUSTED,
    TOKEN_RETRY, TOKEN_FORBIDDEN, TOKEN_STALE } TokenResult;
typedef struct TokenConfig {
    HttpClient *http;
    CoreClock clock;
    CredentialRecord *credentials; /* Exclusive stable borrow; never OAuth token data. */
    const char *client_id, *client_version;
    uint32_t windows_major, windows_build; /* Truthful OS values, supplied by platform owner. */
    const wchar_t *service_origins[TOKEN_ORIGIN_COUNT]; /* Exact trusted HTTPS Spotify service origins. */
    size_t service_origin_count;
    uint32_t challenge_iterations, challenge_timeout_ms; /* 0: 65536/2000; caps 262144/5000. */
    /* Explicit deterministic fixture overrides; both must be IPv4 loopback HTTP. */
    const wchar_t *fixture_clienttoken_url, *fixture_login5_url;
} TokenConfig;
typedef struct TokenInfo { uint64_t expires_at, refresh_at, serial; bool present; } TokenInfo;
typedef struct TokenAuthorization {
    HttpHeader headers[2];
    wchar_t bearer[TOKEN_TEXT_CAP], client[TOKEN_TEXT_CAP];
    uint64_t client_serial, bearer_serial;
    unsigned retries;
    bool issued;
} TokenAuthorization;

/* One network owner. Incoming concurrent users must serialize through that
 * owner; repeated ensure calls join one in-flight refresh. No module thread,
 * heap or OS handle. Arena storage plus validator secret scratch <=32 KiB.
 * Config origin/fixture strings, credentials and HTTP outlive shutdown.
 * HTTP's sensitive-header allowlist must include both auth origins and services.
 * Never modify/reinitialize the borrowed record while the manager is active. */
TokenResult token_manager_create(Arena *secret_arena, const TokenConfig *config, TokenManager **output);
TokenResult token_ensure(TokenManager *manager, TokenKind kind);
TokenResult token_poll(TokenManager *manager); /* One HTTP callback or bounded hashcash slice. */
HANDLE token_wait_handle(const TokenManager *manager); /* Borrowed HTTP event; NULL when no HTTP work. */
uint32_t token_next_wait_ms(const TokenManager *manager); /* 0 only while solving bounded work. */
TokenInfo token_info(const TokenManager *manager, TokenKind kind); /* No token text. */
TokenResult token_result(const TokenManager *manager);
/* Start authorization at {0}; preserve it for the ONE retry, then clear it.
 * Exact origin + granted domain required, regardless of bearer validity.
 * Copies headers into caller storage; use only for this validated URL, no CDN
 * forwarding or logging. HttpClient copies headers synchronously on submit. */
TokenResult token_authorize(TokenManager *manager, const wchar_t *url, TokenAuthorization *authorization);
TokenResult token_report_status(TokenManager *manager, TokenAuthorization *authorization, unsigned status);
void token_authorization_clear(TokenAuthorization *authorization);
TokenResult token_retry_refresh(TokenManager *manager); /* Explicit reset of a latched failure. */
/* Logout also wipes the borrowed record. The account owner first stops AP and
 * deletes persisted credentials using credential_store_logout. Normal shutdown
 * retains the record for secure restart. Both return PENDING until HTTP retires. */
TokenResult token_logout(TokenManager *manager);
TokenResult token_manager_shutdown(TokenManager *manager);
bool token_balanced(const TokenManager *manager);
#endif
