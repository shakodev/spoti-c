#ifndef SPOTIC_PROTOCOL_SHANNON_ADAPTER_H
#define SPOTIC_PROTOCOL_SHANNON_ADAPTER_H

#include "shannon.h"

enum { SHANNON_AP_KEY_SIZE = 32, SHANNON_AP_MAC_SIZE = 4,
       SHANNON_AP_MAX_PLAINTEXT = 3 + 65535 };

typedef enum ShannonResult {
    SHANNON_OK, SHANNON_INVALID_ARGUMENT, SHANNON_BAD_STATE,
    SHANNON_LIMIT, SHANNON_AUTH_FAILED, SHANNON_NONCE_EXHAUSTED
} ShannonResult;

typedef enum ShannonDirection { SHANNON_TX = 1, SHANNON_RX = 2 } ShannonDirection;
typedef enum ShannonPhase { SHANNON_UNINITIALIZED, SHANNON_IDLE, SHANNON_ACTIVE,
                            SHANNON_FAILED } ShannonPhase;

typedef struct ShannonAdapter {
    ShannonCtx cipher;
    uint64_t next_nonce;
    size_t bytes;
    ShannonDirection direction;
    ShannonPhase phase;
} ShannonAdapter;

/* Own two distinct adapters per session, one TX and one RX; each is confined to
 * its owning thread. Do not copy live contexts or overlap buffers with them.
 * Initialize before use; clear at teardown. Invalid calls preserve state.
 * AP nonces start at zero, serialized u32 big-endian. begin reserves a nonce
 * immediately, even if processing is abandoned; counters never wrap. */
ShannonResult shannon_adapter_init(ShannonAdapter *adapter, ShannonDirection direction,
                                   const void *key, size_t size);
ShannonResult shannon_adapter_begin(ShannonAdapter *adapter);
/* In-place command/length/body bytes, at most 65538 per message. Fragmentation
 * is arbitrary; this module does not parse frames or perform socket I/O.
 * RX plaintext MUST remain in caller-owned quarantine until finish_rx returns
 * OK. On authentication failure discard/wipe it and close the session. */
ShannonResult shannon_adapter_update(ShannonAdapter *adapter, void *data, size_t size);
ShannonResult shannon_adapter_finish_tx(ShannonAdapter *adapter, uint8_t mac[4]);
/* Constant-work four-byte comparison. Failure destroys key state and latches
 * FAILED; no further packet processing is permitted until a new session init. */
ShannonResult shannon_adapter_finish_rx(ShannonAdapter *adapter, const uint8_t mac[4]);
void shannon_adapter_clear(ShannonAdapter *adapter);

#endif
