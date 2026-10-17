#ifndef SPOTIC_FIXTURE_HTTP_H
#define SPOTIC_FIXTURE_HTTP_H

#include <winsock2.h>
#include "core/event_queue.h"

#define FIXTURE_HTTP_HEADER_MAX ((size_t)2048)
#define FIXTURE_HTTP_BODY_MAX ((size_t)2048)
#define FIXTURE_HTTP_RESPONSE_MAX ((size_t)4096)

/* One scripted response/connection per start, IPv4 127.0.0.1, ephemeral port.
 * Caller owns this fixed storage; zero-init before first start. No external DNS,
 * account, TLS or Spotify API. All socket I/O is nonblocking and stop-aware. */
typedef struct FixtureHttp {
    SOCKET listener, peer;
    WSAEVENT network;
    HANDLE stop, seen, release, thread;
    MetricsHandle listener_metric, peer_metric, network_metric;
    MetricsHandle stop_metric, seen_metric, release_metric, thread_metric;
    Metrics *metrics;
    uint16_t port;
    bool winsock_started, hold_response;
    char request[FIXTURE_HTTP_HEADER_MAX];
    size_t request_bytes;
    char response[FIXTURE_HTTP_RESPONSE_MAX];
    size_t response_bytes;
    _Atomic DWORD error;
} FixtureHttp;

CoreResult fixture_http_start(FixtureHttp *fixture, Metrics *metrics,
    unsigned status, const void *body, size_t bytes, bool hold_response);
/* Stop interrupts accept/read/held response, joins, closes every owned socket,
 * event/thread and Winsock reference. Timeout preserves state for a later retry. */
CoreResult fixture_http_stop(FixtureHttp *fixture);

typedef struct FixtureEndpoint { uint16_t port; Metrics *metrics; } FixtureEndpoint;
typedef struct FixtureTransport {
    FixtureEndpoint endpoint;
    CoreResult (*exchange)(const FixtureEndpoint *endpoint, const void *request,
        size_t request_bytes, HANDLE cancel, void *response, size_t capacity, size_t *received);
} FixtureTransport;

/* Injectable bounded transport; raw HTTP bytes. Cancellation returns CLOSED,
 * buffer exhaustion FULL, timeout TIMEOUT, disconnect before a reply OS_ERROR.
 * Endpoint is copied so server shutdown never races transport configuration.
 * Caller keeps metrics/cancel/output alive until exchange returns. */
FixtureTransport fixture_http_transport(const FixtureHttp *fixture);
CoreResult fixture_http_exchange(const FixtureEndpoint *endpoint, const void *request,
    size_t request_bytes, HANDLE cancel, void *response, size_t capacity, size_t *received);

#endif
