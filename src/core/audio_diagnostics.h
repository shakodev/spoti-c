#ifndef SPOTIC_AUDIO_DIAGNOSTICS_H
#define SPOTIC_AUDIO_DIAGNOSTICS_H
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/* One serialized writer per metric; any number of snapshot readers. Values
 * are QPC ticks except the explicitly named frame/state/command metrics. */
typedef enum AudioDiagnosticKind {
    AD_CALLBACK_TICKS, AD_CALLBACK_GAP_TICKS, AD_LOW_WATER_FRAMES, AD_UNDERRUN_FRAMES,
    AD_PRODUCER_WAKE_TICKS, AD_PUMP_TICKS, AD_READ_WAIT_TICKS, AD_DECRYPT_TICKS,
    AD_NETWORK_PUMP_TICKS, AD_RANGE_TICKS, AD_CACHE_READ_TICKS, AD_CACHE_WRITE_TICKS,
    AD_CACHE_QUEUE_TICKS, AD_PLAYER_STATE, AD_PLAYER_COMMAND, AD_TRACK_BOUNDARY,
    AD_CALLBACK_FRAMES, AD_SOURCE_RATE, AD_SOURCE_CHANNELS,
    AD_COUNT
} AudioDiagnosticKind;
typedef struct AudioDiagnosticValue {
    uint64_t count, total, maximum, last, last_qpc, maximum_qpc;
} AudioDiagnosticValue;
typedef struct AudioDiagnosticCounter {
    _Atomic uint64_t count, total, maximum, last, last_qpc, maximum_qpc;
} AudioDiagnosticCounter;
typedef struct AudioDiagnostics {
    AudioDiagnosticCounter values[AD_COUNT];
    _Atomic uint64_t invalidations;
} AudioDiagnostics;
_Static_assert(sizeof(AudioDiagnostics) <= 1024, "fixed playback diagnostic budget");
typedef struct AudioDiagnosticSnapshot {
    AudioDiagnosticValue values[AD_COUNT];
    uint64_t invalidations;
} AudioDiagnosticSnapshot;
/* Initialize before publishing to workers. NULL disables all instrumentation,
 * including clock queries. Storage remains owned until every worker is joined. */
bool audio_diagnostics_init(AudioDiagnostics *);
uint64_t audio_diagnostic_now(const AudioDiagnostics *);
void audio_diagnostic_record(AudioDiagnostics *, AudioDiagnosticKind, uint64_t value, uint64_t qpc);
void audio_diagnostic_elapsed(AudioDiagnostics *, AudioDiagnosticKind, uint64_t start);
AudioDiagnosticSnapshot audio_diagnostics_snapshot(const AudioDiagnostics *);
const char *audio_diagnostic_name(AudioDiagnosticKind);
#endif
