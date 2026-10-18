#ifndef SPOTIC_API_TOKEN_INTERNAL_H
#define SPOTIC_API_TOKEN_INTERNAL_H
#include "token_manager.h"
#include "protocol/crypto.h"
#include "protocol/proto_codec.h"
#include "spotify/clienttoken/v0/clienttoken_http.pb.h"
#include "spotify/login5/v3/login5.pb.h"

typedef spotify_clienttoken_http_v0_ClientTokenRequest ClientTokenRequest;
typedef spotify_clienttoken_http_v0_ClientTokenResponse ClientTokenResponse;
typedef spotify_login5_v3_LoginOk TokenLoginOk;
typedef enum TokenStage { TS_IDLE, TS_CLIENT_HTTP, TS_LOGIN_HTTP, TS_CLIENT_HASH,
    TS_LOGIN_HASH, TS_FAILED, TS_CLOSING, TS_CLOSED } TokenStage;
enum { TOKEN_UPLOAD = 4096, TOKEN_RESPONSE = 6144, TOKEN_CONTEXT = 1024,
    TOKEN_PREFIX = 128, TOKEN_HASH_SLICE = 64, TOKEN_DIFFICULTY = 20 };
typedef struct TokenSlot { char text[TOKEN_TEXT_CAP]; TokenInfo info; } TokenSlot;
typedef union TokenScratch {
    ClientTokenRequest client_request;
    ClientTokenResponse client_response;
    TokenLoginOk login_ok;
    spotify_login5_v3_challenges_HashcashChallenge login_challenge;
    wchar_t header[TOKEN_TEXT_CAP];
} TokenScratch;
struct TokenManager {
    TokenConfig config;
    TokenStage stage;
    TokenResult result;
    TokenSlot slots[2];
    uint64_t now, deadline, hash_started, hash_seed, hash_counter, sequence;
    HttpRequestId request_id;
    char client_id[CREDENTIAL_CLIENT_CAP], version[65], domains[8][257], challenge_state[257];
    uint8_t context[TOKEN_CONTEXT], prefix[TOKEN_PREFIX], suffix[16];
    size_t domain_count, context_bytes, prefix_bytes;
    unsigned difficulty, client_rounds, login_rounds;
    bool want_client, want_bearer, http_active, logout, shutdown;
    uint8_t upload[TOKEN_UPLOAD], response[TOKEN_RESPONSE];
    TokenScratch scratch;
};
_Static_assert(sizeof(TokenManager) + PROTO_MAX_BLOB <= TOKEN_SECRET_LIMIT,
    "Token manager and validator secret scratch share the 32 KiB cap");

bool token_text_valid(const char *text, size_t capacity, size_t maximum);
TokenResult token_clock(TokenManager *manager);
TokenResult token_fail(TokenManager *manager, TokenResult result);
void token_wipe_job(TokenManager *manager);
TokenResult token_submit(TokenManager *manager, bool login, size_t bytes);
TokenResult token_schedule(TokenManager *manager);
TokenResult token_install(TokenManager *manager, TokenKind kind, const char *text, uint32_t expiry, uint32_t refresh);
TokenResult token_client_start(TokenManager *manager, bool answer);
TokenResult token_client_response(TokenManager *manager, size_t bytes);
TokenResult token_login_start(TokenManager *manager, bool answer);
TokenResult token_login_response(TokenManager *manager, size_t bytes);
TokenResult token_hash_begin(TokenManager *manager, const void *prefix, size_t bytes, int32_t difficulty, bool login);
TokenResult token_hash_step(TokenManager *manager);
bool token_url_host(const wchar_t *url, bool origin_only, bool fixture, char host[256]);
bool token_domain_valid(const char *domain);
bool token_domain_granted(const TokenManager *manager, const char *host);
#endif
