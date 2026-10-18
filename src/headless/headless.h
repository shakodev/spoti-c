#ifndef SPOTIC_HEADLESS_H
#define SPOTIC_HEADLESS_H
#include "audio/player.h"
#include <stdio.h>

typedef struct Headless Headless;
typedef struct HeadlessConfig {
    const wchar_t *cache_directory; /* Private, existing parent; never fixtures. */
    const char *fixture_url; /* Explicit loopback ciphertext URL; NULL selects account. */
    const wchar_t *credential_path; /* Private DPAPI record for opt-in live mode. */
    const char *country, *catalogue;
    bool wasapi, realtime;
    void (*observe)(void *, const float *, size_t); /* Fake sink, decode owner. */
    void *observe_context;
} HeadlessConfig;
typedef struct HeadlessMetrics {
    uint64_t working_set, peak_working_set, private_working_set, private_commit;
    uint64_t arena_high_water, wakeups, callback_ticks, callbacks, frames, underruns;
    uint64_t network_attempts, cache_hits;
    uint64_t qpc_frequency;
    uint32_t process_handles;
    bool balanced;
} HeadlessMetrics;
/* Fixed VM allocation; explicit failure, no silent pool growth. Opens owners
 * asynchronously, waits for startup. No GUI or audio-callback allocations. */
bool headless_open(const HeadlessConfig *, Headless **);
PlayerResult headless_submit(Headless *, const PlayerCommand *);
PlayerSnapshot headless_snapshot(Headless *);
/* Commands: play, pause, seek <milliseconds>, volume <0..1>, next, stop.
 * Reject trailing input, nonfinite/overflow and out-of-range values. */
bool headless_command(Headless *, const char *);
bool headless_parse(const char *, uint32_t rate, PlayerCommand *);
bool headless_metrics(Headless *, HeadlessMetrics *);
/* Refresh process memory/handle counters, preserving the playback counters.
 * Also usable after shutdown so secret wiping and teardown count in the peak. */
bool headless_process_metrics(HeadlessMetrics *);
bool headless_write_metrics(FILE *, const HeadlessMetrics *);
/* Quiesce producers first. Joins owners, closes all handles, wipes secrets and
 * frees storage. Returns final metrics, including lifecycle balance. */
bool headless_close(Headless *, HeadlessMetrics *);
#endif
