#ifndef SPOTIC_PROTOCOL_AP_SESSION_H
#define SPOTIC_PROTOCOL_AP_SESSION_H
#include "ap_resolve.h"

typedef struct ApSession ApSession;
typedef enum ApState { AP_STATE_RESOLVE, AP_STATE_CONNECT, AP_STATE_HELLO,
    AP_STATE_CHALLENGE, AP_STATE_FRAMED, AP_STATE_AUTHENTICATED,
    AP_STATE_BACKOFF, AP_STATE_CLOSED } ApState;
/* All transport calls are nonblocking and belong to the same network owner.
 * PENDING read/write means would-block; report bytes=0. EOF is CLOSED.
 * stop must retain pending async context until it can return OK. Custom seams
 * are borrowed through shutdown and cannot bypass cryptographic verification. */
typedef struct ApTransport {
    void *context;
    ApResult (*begin)(void *, const ApEndpoint *);
    ApResult (*poll)(void *);
    ApResult (*read)(void *, void *, size_t, size_t *);
    ApResult (*write)(void *, const void *, size_t, size_t *);
    ApResult (*stop)(void *);
    size_t (*handles)(void *, HANDLE *, size_t);
} ApTransport;
typedef void (*ApPacketHandler)(void *context, ApSession *session, const ApPacketView *packet);
typedef struct ApConfig {
    HttpClient *http; /* Same owner; required only for network resolution. */
    const wchar_t *resolver_url; /* Optional controlled-fixture destination override. */
    CoreClock clock;
    Metrics *metrics;
    uint64_t version;
    uint32_t connect_timeout_ms, handshake_timeout_ms, auth_timeout_ms;
    uint32_t max_attempts; /* 1..8; zero defaults to 3. */
    ApTransport transport; /* All NULL selects native asynchronous Winsock. */
    ApPacketHandler on_packet; /* Borrowed verified view, valid during callback. */
    void *context;
} ApConfig;
typedef enum ApEventType { AP_EVENT_HTTP, AP_EVENT_SOCKET, AP_EVENT_TIMER, AP_EVENT_CANCEL } ApEventType;
typedef struct ApEvent { ApEventType type; uint32_t generation; } ApEvent;

/* One network owner, one shared protocol arena <=256 KiB. No UI/audio calls or
 * module heap/thread. Reclaim arena only after shutdown returns OK. */
ApResult ap_session_create(Arena *arena, const ApConfig *config, ApSession **output);
/* Optional previously resolved candidates (copied); NULL performs resolution.
 * New starts are allowed only after the previous CLOSED state has retired. */
ApResult ap_start(ApSession *session, const ApEndpoints *resolved);
ApResult ap_send(ApSession *session, uint8_t command, const void *body, size_t bytes);
ApResult ap_on_event(ApSession *session, ApEvent event);
ApState ap_state(const ApSession *session);
ApResult ap_result(const ApSession *session);
uint32_t ap_generation(const ApSession *session);
uint32_t ap_next_wait_ms(const ApSession *session);
/* Borrow handles; associate readiness with the captured session generation.
 * Always handle due deadlines too. No periodic polling is needed. */
size_t ap_wait_handles(ApSession *session, HANDLE *handles, size_t capacity);
/* Permanently closes start admission and cancels; PENDING retains DNS/HTTP storage. Re-drive via event
 * handles until OK; there is no blocking join inside this module. */
ApResult ap_session_shutdown(ApSession *session);
#endif
