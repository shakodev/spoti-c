#ifndef SPOTIC_API_OAUTH_INTERNAL_H
#define SPOTIC_API_OAUTH_INTERNAL_H
#include <winsock2.h>
#include "oauth.h"
#include "json_base.h"
#include "protocol/crypto.h"

enum { OAUTH_REQUEST_LIMIT = 4096, OAUTH_UPLOAD_LIMIT = 12288 };
struct OAuthFlow {
    CoreClock clock;
    OAuthState state;
    OAuthResult result;
    uint32_t timeout_ms;
    uint64_t deadline, peer_deadline;
    char client_id[CREDENTIAL_CLIENT_CAP], registered[256], redirect[256], path[128], host[64], scopes[513];
    char verifier[129], challenge[44], state_key[44], code[1025], authorization[4096];
    char refresh[CREDENTIAL_REFRESH_CAP], request[OAUTH_REQUEST_LIMIT + 1], upload[OAUTH_UPLOAD_LIMIT];
    char response[JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE];
    uint64_t json_pool[96 * 1024 / sizeof(uint64_t)];
    OAuthToken token;
    wchar_t token_url[256];
    HttpClient *http;
    HttpRequestId request_id;
    SOCKET listener, peer;
    WSAEVENT listen_event, peer_event;
    size_t request_bytes, live_sockets, live_events;
    uint16_t port;
    bool winsock, http_active, refreshing, taken, destroyed, cleanup_failed;
};
typedef struct OAuthWriter { char *data; size_t capacity, used; bool full; } OAuthWriter;
bool oauth_unreserved(char c);
void oauth_write(OAuthWriter *writer, const char *text);
void oauth_pair(OAuthWriter *writer, const char *name, const char *value);
OAuthResult oauth_terminal(OAuthFlow *flow, OAuthResult result);
void oauth_wipe_transient(OAuthFlow *flow);
OAuthResult oauth_loopback_open(OAuthFlow *flow);
void oauth_loopback_close(OAuthFlow *flow);
OAuthResult oauth_loopback_poll(OAuthFlow *flow);
OAuthResult oauth_callback_request(OAuthFlow *flow, const char *request, size_t bytes);
OAuthResult oauth_token_poll(OAuthFlow *flow);
#endif
