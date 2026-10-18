#ifndef SPOTIC_DEALER_H
#define SPOTIC_DEALER_H
#include "connect_state.h"
#include "ap_resolve.h"

typedef enum DealerFrameType { DEALER_READY, DEALER_TEXT_PART, DEALER_TEXT_END,
    DEALER_BINARY, DEALER_PEER_CLOSE, DEALER_WRITTEN } DealerFrameType;
typedef struct DealerFrame { DealerFrameType type; const void *data; size_t bytes; unsigned status; } DealerFrame;
typedef struct DealerTransport {
    DealerResult (*open)(void *, const ApEndpoint *, const wchar_t *secret_path);
    DealerResult (*poll)(void *, DealerFrame *);
    DealerResult (*send)(void *, const void *, size_t);
    DealerResult (*close)(void *); /* OK only after callbacks/buffer leases retire. */
    HANDLE (*event)(void *);
} DealerTransport;
typedef struct DealerConfig {
    ConnectState *connect; TokenManager *tokens; CoreClock clock; ApEndpoint endpoint;
    const DealerTransport *transport; void *transport_context;
    /* Explicit offline fixture credential seam. Never used with native transport. */
    DealerResult (*fixture_token)(void *, wchar_t *, size_t, uint64_t *);
    void *fixture_context;
} DealerConfig;
typedef struct DealerClient DealerClient;
typedef enum DealerPhase { DEALER_AUTHORIZING, DEALER_CONNECTING, DEALER_ONLINE,
    DEALER_RETIRING, DEALER_BACKOFF, DEALER_STOPPED } DealerPhase;
typedef struct DealerSnapshot { DealerPhase phase; DealerResult last_error; uint64_t reconnects; size_t assembled; } DealerSnapshot;
/* Core metadata + exactly 256 KiB borrowed protocol scratch, shared exclusively
 * with other protocol parsers. No heap/thread. One network owner. The native
 * WinHTTP transport is selected when transport=NULL. Config remains borrowed. */
DealerResult dealer_create(Arena *core, Arena *protocol, const DealerConfig *, DealerClient **);
DealerResult dealer_pump(DealerClient *);
uint32_t dealer_next_wait_ms(DealerClient *);
HANDLE dealer_wait_handle(DealerClient *);
size_t dealer_wait_handles(DealerClient *, HANDLE output[3]); /* WS + shared HTTP + player state. */
DealerSnapshot dealer_snapshot(DealerClient *); /* Network owner, no secrets. */
DealerResult dealer_shutdown(DealerClient *);
/* Same fragmented-message parser used by native transport and replay tests.
 * Reply is copied into caller storage. All command results get a boolean ack. */
DealerResult dealer_frame(DealerClient *, const void *, size_t, bool final, char *reply, size_t capacity, size_t *written);
#endif
