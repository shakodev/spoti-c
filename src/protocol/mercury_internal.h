#ifndef SPOTIC_MERCURY_INTERNAL_H
#define SPOTIC_MERCURY_INTERNAL_H
#include "mercury.h"
#include "proto_codec.h"
#include "mercury.pb.h"
#include "core/memory_budget.h"
#include <string.h>
enum { MERCURY_ACCUMULATORS = MERCURY_REQUESTS + MERCURY_EVENTS };
typedef struct MercuryPending {
    bool live, partial;
    uint8_t command;
    int subscription;
    uint64_t sequence, deadline;
    uint64_t listeners[MERCURY_SUBSCRIPTIONS]; /* IDs active at event admission. */
    size_t offset, bytes, count, fragments, lengths[MERCURY_PARTS];
    char uri[MERCURY_URI + 1];
    MercuryCallback callback;
    void *context;
} MercuryPending;
typedef struct MercurySub {
    bool live, active, waiting;
    uint64_t id;
    char uri[MERCURY_URI + 1], aliases[MERCURY_ALIASES][MERCURY_URI + 1];
    size_t count;
    MercuryCallback callback;
    void *context;
} MercurySub;
struct MercuryClient {
    MercuryConfig config;
    bool connected, busy;
    uint32_t generation;
    uint64_t sequence, recent[MERCURY_REQUESTS];
    size_t recent_next, recent_count, used, high_water;
    MercuryPending pending[MERCURY_ACCUMULATORS];
    MercurySub subs[MERCURY_SUBSCRIPTIONS];
    uint8_t pool[MERCURY_POOL], tx[65535];
};
_Static_assert(sizeof(MercuryClient) <= MEMORY_BUDGET_METADATA, "Mercury metadata pool budget");
bool mercury_text(const char *text, char output[MERCURY_URI + 1], bool empty);
ApResult mercury_start(MercuryClient *, uint8_t command, const char *method,
    const char *uri, const char *content_type, const MercurySpan *, size_t,
    uint64_t, MercuryCallback, void *, int subscription, MercuryId *);
void mercury_release(MercuryClient *, MercuryPending *);
void mercury_finish(MercuryClient *, MercuryPending *, ApResult, const MercuryResponse *);
ApResult mercury_dispatch(MercuryClient *, const ApPacketView *);
ApResult mercury_complete(MercuryClient *, MercuryPending *);
#endif
