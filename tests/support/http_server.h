#ifndef SPOTIC_TEST_HTTP_SERVER_H
#define SPOTIC_TEST_HTTP_SERVER_H
#include <winsock2.h>
#include "core/metrics.h"

#define TEST_HTTP_RESPONSES 16u
typedef struct TestHttpResponse {
    const char *headers;
    const void *body;
    size_t bytes, fragment;
    bool hold_headers, hold_body;
    bool capture_upload; /* Capture bounded Content-Length body before responding. */
    unsigned char fill; /* body==NULL: generate bytes without a body allocation. */
} TestHttpResponse;
typedef struct TestHttpServer {
    SOCKET listener, peer;
    WSAEVENT network;
    HANDLE stop, seen, headers_sent, release_headers, release_body, thread;
    MetricsHandle leases[9];
    Metrics *metrics;
    uint16_t port;
    const TestHttpResponse *responses;
    size_t response_count;
    char requests[TEST_HTTP_RESPONSES][4096];
    _Atomic unsigned accepted;
    _Atomic DWORD error;
    bool winsock;
} TestHttpServer;
bool test_http_server_start(TestHttpServer *server, Metrics *metrics,
    const TestHttpResponse *responses, size_t count);
void test_http_server_stop(TestHttpServer *server);
#endif
