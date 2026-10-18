#ifndef SPOTIC_CONNECT_STATE_H
#define SPOTIC_CONNECT_STATE_H
#include "dealer_payload.h"
#include "audio/player.h"
#include "api/token_manager.h"

typedef struct ConnectState ConnectState;
typedef struct ConnectConfig {
    Player *player; HttpClient *http; TokenManager *tokens; CoreClock clock;
    const char *device_id, *name, *client_id;
    const wchar_t *spclient_origin; /* Exact trusted discovered HTTPS origin. */
    bool fixture_replay; /* Deterministic transport-free tests only. */
    const wchar_t *fixture_http_origin; /* Explicit IPv4 loopback HTTP; synthetic auth only. */
} ConnectConfig;
typedef struct ConnectSnapshot {
    PlayerSnapshot player;
    char active_device_id[257];
    uint64_t revision;
    bool registered, active, logged_out;
    uint32_t last_message_id;
    uint32_t controls; /* Accepted controls: play/pause/seek/next/previous/transfer/volume bits 0..6. */
} ConnectSnapshot;
/* One network owner; no module thread/heap. Config borrows outlive shutdown.
 * UI takes immutable copies via connect_snapshot. Protocol scratch belongs to
 * Dealer and is borrowed only during message/command calls. */
DealerResult connect_create(Arena *, const ConnectConfig *, ConnectState **);
DealerResult connect_connection(ConnectState *, const char *connection_id);
DealerResult connect_command(ConnectState *, char *, size_t, void *scratch);
DealerResult connect_message(ConnectState *, const char *uri, const void *, size_t);
DealerResult connect_encode(ConnectState *, void *, size_t, size_t *);
DealerResult connect_poll(ConnectState *); /* Coalesced PUT, one fixed request slot. */
void connect_changed(ConnectState *); /* After local controls/snapshot changes. */
ConnectSnapshot connect_snapshot(ConnectState *);
DealerResult connect_shutdown(ConnectState *); /* Cancel/drain PUT before arena reclamation. */
#endif
