#ifndef SPOTIC_PROTOCOL_AUDIO_KEY_H
#define SPOTIC_PROTOCOL_AUDIO_KEY_H
#include "ap_packet.h"
#include "core/arena.h"
#include "core/clock.h"

enum { AUDIO_KEY_REQUESTS = 8, AUDIO_KEY_REQUEST = 0x0c,
    AUDIO_KEY_RESPONSE = 0x0d, AUDIO_KEY_ERROR = 0x0e };
typedef struct AudioKeyClient AudioKeyClient;
typedef struct AudioKeyId { uint32_t value; } AudioKeyId;
/* key is non-NULL only on AP_OK, borrowed ONLY through this callback. Copy it
 * into a bounded, generation-owned media record before returning. Mutations
 * from callbacks/send are rejected with AP_BAD_STATE. */
typedef void (*AudioKeyCallback)(void *, AudioKeyId, ApResult, const uint8_t key[16]);
typedef struct AudioKeyConfig {
    /* Synchronous copy/enqueue, normally an ap_send adapter. AP_OK accepts the
     * entire packet; any other result accepts nothing. Must not reenter. */
    ApResult (*send)(void *, uint8_t command, const void *, size_t);
    void *context;
    CoreClock clock;
    uint32_t timeout_ms; /* Zero defaults to 1500 milliseconds. */
} AudioKeyConfig;
typedef struct AudioKeyStats { size_t pending, pending_high_water; } AudioKeyStats;

/* Single network owner; one arena allocation, no heap/handles/threads and no
 * per-packet arena allocations. Keep arena/config context alive through final
 * disconnect. IDs never wrap/reuse, including rejected sends and reconnects. */
ApResult audio_key_create(Arena *, const AudioKeyConfig *, AudioKeyClient **);
/* AP may retry with the same generation; IDs remain unique across every
 * connection. Older generations cannot be connected or dispatch replies. */
ApResult audio_key_connect(AudioKeyClient *, uint32_t ap_generation);
/* Complete each pending request once with AP_RECONNECT or AP_CLOSED. No replay. */
ApResult audio_key_disconnect(AudioKeyClient *, bool reconnect);
/* Caller deadline can only tighten the configured response timeout. NEVER
 * selects that timeout, and a longer enclosing media deadline cannot extend it. */
ApResult audio_key_request(AudioKeyClient *, const uint8_t gid[16], const uint8_t file_id[20],
    uint64_t deadline, AudioKeyCallback, void *context, AudioKeyId *);
ApResult audio_key_cancel(AudioKeyClient *, AudioKeyId);
/* Peeks authenticated AP storage itself, does not consume. Exact 20-byte key
 * and 6-byte error bodies are required. Matching malformed replies terminate
 * once with AP_MALFORMED; unknown/late IDs return AP_STALE without callbacks.
 * A key error completes with AP_IO_ERROR; no server payload is retained. */
ApResult audio_key_on_packet(AudioKeyClient *, uint32_t ap_generation, const ApPacket *);
ApResult audio_key_tick(AudioKeyClient *);
uint32_t audio_key_next_wait_ms(const AudioKeyClient *);
AudioKeyStats audio_key_stats(const AudioKeyClient *);
#endif
