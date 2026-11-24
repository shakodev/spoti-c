#include "audio_diagnostics.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

bool audio_diagnostics_init(AudioDiagnostics *d)
{
    if (!d) { return false; }
    for (unsigned i = 0; i < AD_COUNT; ++i) {
        AudioDiagnosticCounter *v = &d->values[i];
        atomic_init(&v->count, 0); atomic_init(&v->total, 0); atomic_init(&v->maximum, 0);
        atomic_init(&v->last, 0); atomic_init(&v->last_qpc, 0); atomic_init(&v->maximum_qpc, 0);
    }
    atomic_init(&d->invalidations, 0);
    return atomic_is_lock_free(&d->invalidations) && atomic_is_lock_free(&d->values[0].count);
}
uint64_t audio_diagnostic_now(const AudioDiagnostics *d)
{
    if (!d) { return 0; }
    LARGE_INTEGER now; QueryPerformanceCounter(&now); return (uint64_t)now.QuadPart;
}
void audio_diagnostic_record(AudioDiagnostics *d, AudioDiagnosticKind kind, uint64_t value, uint64_t qpc)
{
    if (!d || (unsigned)kind >= AD_COUNT) { return; }
    AudioDiagnosticCounter *v = &d->values[kind];
    /* The writer is unique. No CAS/retry loop on the callback path. */
    atomic_store_explicit(&v->count, atomic_load_explicit(&v->count, memory_order_relaxed) + 1, memory_order_relaxed);
    atomic_store_explicit(&v->total, atomic_load_explicit(&v->total, memory_order_relaxed) + value, memory_order_relaxed);
    if (value > atomic_load_explicit(&v->maximum, memory_order_relaxed)) {
        atomic_store_explicit(&v->maximum, value, memory_order_relaxed);
        atomic_store_explicit(&v->maximum_qpc, qpc, memory_order_relaxed);
    }
    atomic_store_explicit(&v->last, value, memory_order_relaxed);
    atomic_store_explicit(&v->last_qpc, qpc, memory_order_relaxed);
}
void audio_diagnostic_elapsed(AudioDiagnostics *d, AudioDiagnosticKind kind, uint64_t start)
{
    if (!d) { return; }
    uint64_t now = audio_diagnostic_now(d);
    audio_diagnostic_record(d, kind, now >= start ? now - start : 0, now);
}
AudioDiagnosticSnapshot audio_diagnostics_snapshot(const AudioDiagnostics *d)
{
    AudioDiagnosticSnapshot out = {0}; if (!d) { return out; }
    for (unsigned i = 0; i < AD_COUNT; ++i) {
        const AudioDiagnosticCounter *v = &d->values[i];
        out.values[i] = (AudioDiagnosticValue){atomic_load(&v->count), atomic_load(&v->total),
            atomic_load(&v->maximum), atomic_load(&v->last), atomic_load(&v->last_qpc), atomic_load(&v->maximum_qpc)};
    }
    out.invalidations = atomic_load(&d->invalidations); return out;
}
const char *audio_diagnostic_name(AudioDiagnosticKind kind)
{
    static const char *const names[AD_COUNT] = {"callback_ticks", "callback_gap_ticks", "low_water_frames",
        "underrun_frames", "producer_wake_ticks", "pump_ticks", "read_wait_ticks", "decrypt_ticks",
        "network_pump_ticks", "range_ticks", "cache_read_ticks", "cache_write_ticks", "cache_queue_ticks",
        "player_state", "player_command", "track_boundary", "callback_frames", "source_rate", "source_channels"};
    return (unsigned)kind < AD_COUNT ? names[kind] : "invalid";
}
