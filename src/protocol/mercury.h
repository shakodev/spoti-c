#ifndef SPOTIC_PROTOCOL_MERCURY_H
#define SPOTIC_PROTOCOL_MERCURY_H
#include "ap_packet.h"
#include "core/arena.h"
#include "core/clock.h"

enum { MERCURY_REQUESTS = 16, MERCURY_SUBSCRIPTIONS = 8, MERCURY_EVENTS = 4,
    MERCURY_PARTS = 16, MERCURY_ALIASES = 8, MERCURY_POOL = 128 * 1024,
    MERCURY_RESPONSE_MAX = 32 * 1024, MERCURY_FRAGMENTS = 64, MERCURY_URI = 256 };
enum { MERCURY_REQ = 0xb2, MERCURY_SUB = 0xb3, MERCURY_UNSUB = 0xb4, MERCURY_EVENT = 0xb5 };
typedef struct MercuryClient MercuryClient;
typedef struct MercuryId { uint64_t value; } MercuryId;
typedef struct MercurySpan { const uint8_t *data; size_t bytes; } MercurySpan;
typedef enum MercuryMethod { MERCURY_GET, MERCURY_SEND } MercuryMethod;
typedef struct MercuryResponse {
    const char *uri;
    int32_t status;
    bool event;
    size_t count;
    MercurySpan parts[MERCURY_PARTS - 1];
} MercuryResponse;
/* All response strings/spans are borrowed ONLY through this call. Copy bounded
 * application records before returning. Queue subsequent work: mutations from
 * callbacks/send are rejected with BAD_STATE, preserving every borrowed span. */
typedef void (*MercuryCallback)(void *, MercuryId, ApResult, const MercuryResponse *);
typedef struct MercuryConfig {
    /* Synchronous copy/enqueue, normally an adapter to ap_send. OK accepts the
     * entire packet; any other result accepts nothing. Must not reenter Mercury. */
    ApResult (*send)(void *, uint8_t, const void *, size_t);
    void *context;
    CoreClock clock;
    bool resubscribe;
    uint32_t timeout_ms; /* Zero defaults to 10 seconds; also bounds event assembly. */
} MercuryConfig;
typedef struct MercuryStats {
    size_t pending, subscriptions, events, pool_used, pool_high_water;
} MercuryStats;

/* Single network owner; one arena allocation, no module heap/handles/threads.
 * Keep arena/config context alive through disconnect. IDs never wrap/reuse.
 * SUB callbacks report acknowledgements, events and disconnects using the same
 * stable subscription ID. Successful UNSUB admission stops local events at once. */
ApResult mercury_create(Arena *, const MercuryConfig *, MercuryClient **);
ApResult mercury_connect(MercuryClient *, uint32_t ap_generation);
ApResult mercury_disconnect(MercuryClient *, bool reconnect);
ApResult mercury_request(MercuryClient *, MercuryMethod, const char *uri,
    const char *content_type, const MercurySpan *parts, size_t count,
    uint64_t deadline, MercuryCallback, void *context, MercuryId *);
ApResult mercury_subscribe(MercuryClient *, const char *uri, uint64_t deadline,
    MercuryCallback, void *context, MercuryId *);
ApResult mercury_unsubscribe(MercuryClient *, MercuryId subscription, uint64_t deadline,
    MercuryCallback, void *context, MercuryId *);
ApResult mercury_cancel(MercuryClient *, MercuryId);
/* Peeks the authenticated packet itself; unverified/failed packets cannot
 * dispatch. AP callbacks can use ap_dispatch_mercury instead. Does not consume. */
ApResult mercury_on_packet(MercuryClient *, uint32_t ap_generation, const ApPacket *);
/* Drive on nearest deadline and transport readiness (including writable after
 * FULL). Replay only subscriptions after reconnect, never GET/SEND/UNSUB.
 * FULL replay waits for transport readiness; next_wait does not busy-poll. */
ApResult mercury_tick(MercuryClient *);
uint32_t mercury_next_wait_ms(const MercuryClient *);
MercuryStats mercury_stats(const MercuryClient *);
#endif
