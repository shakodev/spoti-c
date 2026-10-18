#ifndef SPOTIC_TEST_MERCURY_H
#define SPOTIC_TEST_MERCURY_H
#include "protocol/mercury_internal.h"
#include "core/metrics.h"
#include "support/test_check.h"
typedef struct MercuryRecord {
    MercuryId id;
    ApResult result;
    bool response, event;
    int32_t status;
    char uri[257];
    size_t count, lengths[MERCURY_PARTS], copied;
    uint8_t data[MERCURY_RESPONSE_MAX];
} MercuryRecord;
typedef struct MercuryFixture {
    _Alignas(16) uint8_t storage[256 * 1024];
    Arena arena;
    Metrics metrics;
    MetricsArena binding;
    MercuryClient *client;
    ApPacket local, remote;
    uint64_t now;
    uint32_t generation;
    ApResult send_result;
    size_t sends, sent_bytes, records, arena_used;
    uint8_t sent_command, sent[65535];
    MercuryRecord record[80];
    bool reenter;
} MercuryFixture;
extern MercuryFixture mf;
extern const uint8_t mercury_header[11];
void mercury_fixture_open(bool resubscribe);
void mercury_fixture_close(void);
void mercury_record(void *, MercuryId, ApResult, const MercuryResponse *);
MercuryId mercury_get(uint64_t deadline);
size_t mercury_frame(uint8_t *, uint64_t seq, uint8_t flags, const MercurySpan *, size_t);
ApResult mercury_deliver(uint8_t command, const void *, size_t);
ApResult mercury_reply(uint8_t command, uint64_t seq, uint8_t flags, const MercurySpan *, size_t);
ApResult mercury_ok(uint8_t command, MercuryId);
void mercury_limit_tests(void);
void mercury_subscription_tests(void);
#endif
