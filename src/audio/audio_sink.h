#ifndef SPOTIC_AUDIO_SINK_H
#define SPOTIC_AUDIO_SINK_H
#include "audio_format.h"
#include "core/spsc_ring.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef enum SinkState { SINK_STOPPED, SINK_RUNNING, SINK_PAUSED, SINK_LOST } SinkState;
typedef struct SinkClock {
    uint64_t position, frequency, qpc_100ns, latency_100ns;
    uint32_t padding_frames;
    bool valid, latency_valid;
} SinkClock;
enum { SINK_TIMELINE_SPANS = 256 };
typedef struct SinkSpan { uint64_t output_start, media_start; uint32_t frames, media; } SinkSpan;
typedef struct SinkInfo {
    AudioFormat format;
    SinkState state;
    SinkClock clock;
    uint64_t media_frames, submitted_frames, underrun_frames, producer_wakes;
    uint64_t clock_epoch; /* Changes on start/resume; backend stop may reset its clock. */
    uint64_t played_media_frames;
    bool timeline_valid, timeline_overflow;
    size_t device_bytes, device_peak, os_buffer_bytes;
    uint32_t callback_violations;
} SinkInfo;
typedef struct AudioSink AudioSink;
/* Injection seam. open/close own all backend resources, including on failure.
 * stop joins callbacks even on device loss. clock runs only on the owner.
 * open must not render; start may synchronously render. */
typedef struct SinkBackend {
    AudioResult (*open)(void *, AudioSink *, AudioFormat *, size_t *os_bytes);
    AudioResult (*start)(void *);
    AudioResult (*stop)(void *);
    void (*close)(void *);
    SinkClock (*clock)(void *);
} SinkBackend;
struct AudioSink {
    SpscRing ring;
    AudioFormat format;
    const SinkBackend *backend;
    void *backend_context, *pcm;
    HANDLE producer_event, control_event;
    DWORD owner;
    size_t low_water, os_buffer_bytes;
    _Atomic unsigned state, producer_gate, gain, wake_armed, invalidated;
    _Atomic uint64_t media_frames, submitted_frames, underrun_frames, producer_wakes;
    _Atomic size_t device_bytes, device_peak;
    _Atomic unsigned callback_violations;
    SinkClock paused_clock;
    uint64_t clock_epoch;
    SinkSpan spans[SINK_TIMELINE_SPANS];
    _Atomic uint64_t span_read, span_write;
    _Atomic unsigned span_overflow;
    uint64_t played_media, output_base;
    bool played_valid;
    bool opened;
};
/* Zero-initialize and do not copy a live sink. Owner must be a non-GUI worker.
 * pcm points to exactly 256 KiB caller storage, disjoint from sink metadata.
 * NULL backend selects production WASAPI; otherwise all operations required. */
AudioResult sink_open(AudioSink *, void *pcm, const SinkBackend *, void *context);
AudioResult sink_start(AudioSink *);
AudioResult sink_prepare(AudioSink *); /* Owner: reopen PCM admission while stopped, for prebuffering. */
AudioResult sink_pause(AudioSink *);
AudioResult sink_resume(AudioSink *);
AudioResult sink_stop(AudioSink *);
AudioResult sink_switch(AudioSink *); /* Stop and join producer first. Drops old PCM; stopped on success. */
AudioResult sink_close(AudioSink *); /* Producer thread must already be joined. */
AudioResult sink_set_volume(AudioSink *, float); /* Owner only, finite values clamped. */
SinkInfo sink_info(AudioSink *); /* Owner only; empty snapshot on misuse. */
/* Single decoder producer only. Stop rejects future writes and wakes waiters.
 * Short writes return FULL and retain caller's tail. Never decode in render. */
AudioResult sink_write(AudioSink *, const float *, size_t frames, size_t *written);
/* Fill above low water, then arm. true => wait on producer_event or cancel;
 * false => already low or stopped, handle immediately. No polling needed. */
bool sink_producer_arm(AudioSink *);
/* Backend-only entries: bounded render; asynchronous loss posts control event. */
void sink_render(AudioSink *, float *output, uint32_t frames);
void sink_device_lost(AudioSink *);
/* Tracked backend allocation seam; callback attempts fail and are counted.
 * Audit lock hook is for adapters/tests, never acquire a lock in render. */
void *sink_device_alloc(AudioSink *, size_t);
void sink_device_free(AudioSink *, void *);
bool sink_audit_lock(AudioSink *);
/* Internal callback/owner ledger; no allocation, lock, or signaling. */
void sink_timeline_record(AudioSink *, uint64_t output, uint64_t media, uint32_t frames, uint32_t got);
void sink_timeline_query(AudioSink *, SinkInfo *);
extern const SinkBackend sink_wasapi_backend;
#endif
