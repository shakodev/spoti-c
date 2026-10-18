#ifndef SPOTIC_PROTOCOL_AUDIO_KEY_INTERNAL_H
#define SPOTIC_PROTOCOL_AUDIO_KEY_INTERNAL_H
#include "audio_key.h"

typedef struct AudioKeyPending {
    uint32_t sequence;
    uint64_t deadline;
    AudioKeyCallback callback;
    void *context;
} AudioKeyPending;
struct AudioKeyClient {
    AudioKeyConfig config;
    uint32_t generation, sequence;
    bool connected, busy;
    AudioKeyStats stats;
    AudioKeyPending pending[AUDIO_KEY_REQUESTS];
};
_Static_assert(sizeof(AudioKeyClient) <= 1024, "AudioKey fixed control state budget");
#endif
