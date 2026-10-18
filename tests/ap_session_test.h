#ifndef SPOTIC_AP_SESSION_TEST_H
#define SPOTIC_AP_SESSION_TEST_H
#include "protocol/ap_internal.h"
#include "ap_test.h"
#include "ap_vectors.h"

typedef struct FakeAp {
    uint8_t input[4096], output[4096];
    size_t available, read_at, written, write_limit, fragment;
    unsigned begins, stops, fail_begins, callbacks;
    ApEndpoint chosen[8];
    HANDLE wake;
    MetricsHandle wake_metric;
    bool connected, eof, stop_pending, stop_released;
} FakeAp;
typedef struct ApTestFixture {
    Arena arena;
    Metrics metrics;
    MetricsArena binding;
    ApSession *session;
    FakeAp fake;
    uint64_t now;
    ApEndpoints endpoints;
    ApPacket server;
} ApTestFixture;
extern ApTestFixture ap_fixture;
void ap_fixture_open(void);
void ap_fixture_close(void);
void ap_fixture_start(void);
void ap_fixture_framed(void);
void ap_fixture_authenticated(void);
ApResult ap_fixture_event(ApEventType type);
void ap_fixture_server_packet(uint8_t command, const void *body, size_t bytes, bool corrupt);
void test_ap_keepalive(void);
#endif
