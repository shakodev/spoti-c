#ifndef SPOTIC_TEST_AP_SERVER_H
#define SPOTIC_TEST_AP_SERVER_H
#include <winsock2.h>
#include "core/metrics.h"
typedef struct TestApServer {
    SOCKET listener, peer;
    WSAEVENT network;
    HANDLE stop, seen, release, thread;
    Metrics *metrics;
    MetricsHandle leases[7];
    uint16_t port;
    uint8_t request[512];
    size_t request_bytes, expected_bytes;
    const uint8_t *response;
    size_t response_bytes;
    bool held;
} TestApServer;
/* expected_bytes=0 parses a bounded hello's six-byte wire header. */
void test_ap_server_start(TestApServer *server, Metrics *metrics, size_t expected_bytes,
    const uint8_t *response, size_t response_bytes, bool held);
void test_ap_server_stop(TestApServer *server);
#endif
