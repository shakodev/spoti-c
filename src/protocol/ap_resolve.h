#ifndef SPOTIC_PROTOCOL_AP_RESOLVE_H
#define SPOTIC_PROTOCOL_AP_RESOLVE_H
#include "ap_packet.h"
#include "api/http_client.h"

#define AP_ENDPOINT_COUNT 8u
#define AP_HOST_CHARS 254u
typedef struct ApEndpoint { char host[AP_HOST_CHARS]; uint16_t port; } ApEndpoint;
typedef struct ApEndpoints {
    ApEndpoint accesspoint[AP_ENDPOINT_COUNT], dealer[AP_ENDPOINT_COUNT], spclient[AP_ENDPOINT_COUNT];
    size_t accesspoint_count, dealer_count, spclient_count;
} ApEndpoints;

/* Parse strict bounded host:port arrays, preserving order. No fallback guesses,
 * silent truncation, heap, retained DOM or borrowed endpoint strings. Output is
 * unchanged on failure. Input/pool are exclusively borrowed through this call.
 * Uses the existing shared protocol scratch allowance via json_base. */
ApResult ap_resolve_parse(char *input, size_t bytes, size_t capacity, void *pool,
    size_t pool_bytes, ApEndpoints *output);
bool ap_endpoint_valid(const ApEndpoint *endpoint);
/* Submit the verified official resolver GET (docs/specs/ap-session.md).
 * Caller buffers survive HTTP retirement. No authentication headers are added. */
ApResult ap_resolve_start(HttpClient *http, void *body, size_t capacity, HttpRequestId *id);
/* Explicit destination seam for controlled fixtures; HTTP's TLS/origin policy
 * still applies. Production uses ap_resolve_start's verified destination. */
ApResult ap_resolve_start_at(HttpClient *http, const wchar_t *url, void *body,
    size_t capacity, HttpRequestId *id);
#endif
