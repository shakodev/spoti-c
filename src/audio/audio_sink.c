#include "audio_sink.h"
#include "core/thread.h"
#include <math.h>
#include <string.h>

static bool owner(const AudioSink *s)
{
    return s != NULL && s->opened && s->owner == GetCurrentThreadId();
}
static bool producer_enter(AudioSink *s)
{
    unsigned expected = 1;
    return atomic_compare_exchange_strong(&s->producer_gate, &expected, 3);
}
static void producer_leave(AudioSink *s)
{
    atomic_fetch_and(&s->producer_gate, ~2u);
    WakeByAddressAll((void *)&s->producer_gate);
}
AudioResult sink_open(AudioSink *s, void *pcm, const SinkBackend *backend, void *context)
{
    return sink_open_with_buffer(s,pcm,MEMORY_BUDGET_PCM,backend,context);
}
AudioResult sink_open_with_buffer(AudioSink *s, void *pcm, size_t pcm_bytes, const SinkBackend *backend, void *context)
{
    if (s == NULL || pcm == NULL || s->opened || core_thread_is_gui()) { return AUDIO_INVALID; }
    if (pcm_bytes < 4096 || pcm_bytes > MEMORY_BUDGET_PCM || (pcm_bytes & (pcm_bytes-1))) { return AUDIO_INVALID; }
    if (backend == NULL) { backend = &sink_wasapi_backend; }
    if (!backend->open || !backend->start || !backend->stop || !backend->close || !backend->clock) {
        return AUDIO_INVALID;
    }
    memset(s, 0, sizeof(*s));
    s->backend = backend; s->backend_context = context; s->pcm = pcm; s->pcm_bytes = pcm_bytes;
    s->owner = GetCurrentThreadId();
    atomic_init(&s->state, SINK_STOPPED); atomic_init(&s->gain, 65536);
    atomic_init(&s->invalidated, 0);
    atomic_init(&s->producer_gate, 0); atomic_init(&s->wake_armed, 0);
    atomic_init(&s->media_frames, 0); atomic_init(&s->submitted_frames, 0);
    atomic_init(&s->underrun_frames, 0); atomic_init(&s->producer_wakes, 0);
    atomic_init(&s->callback_ticks, 0); atomic_init(&s->callback_count, 0);
    atomic_init(&s->device_bytes, 0); atomic_init(&s->device_peak, 0);
    atomic_init(&s->callback_violations, 0);
    atomic_init(&s->span_read, 0); atomic_init(&s->span_write, 0); atomic_init(&s->span_overflow, 0);
    if (!atomic_is_lock_free(&s->media_frames) || !atomic_is_lock_free(&s->state) ||
        !atomic_is_lock_free(&s->device_bytes)) { return AUDIO_UNSUPPORTED; }
    s->producer_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->control_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s->producer_event || !s->control_event) {
        if (s->producer_event) { CloseHandle(s->producer_event); }
        if (s->control_event) { CloseHandle(s->control_event); }
        s->producer_event = s->control_event = NULL; return AUDIO_NO_MEMORY;
    }
    AudioResult r = backend->open(context, s, &s->format, &s->os_buffer_bytes);
    if (r == AUDIO_OK && (s->format.sample_format != AUDIO_FLOAT32 ||
        s->format.sample_rate == 0 || s->format.channels == 0 || s->format.channels > 2)) {
        r = AUDIO_UNSUPPORTED;
    }
    if (r == AUDIO_OK && !spsc_init_pcm(&s->ring, pcm, pcm_bytes,
        s->format.channels * sizeof(float))) { r = AUDIO_INVALID; }
    if (r == AUDIO_OK) {
        atomic_store(&s->producer_gate, 1);
        if (atomic_load(&s->invalidated)) { r = AUDIO_BAD_STATE; }
    }
    if (r != AUDIO_OK) {
        backend->close(s->backend_context);
        atomic_store(&s->producer_gate, 0);
        CloseHandle(s->producer_event); CloseHandle(s->control_event);
        s->producer_event = s->control_event = NULL; return r;
    }
    s->low_water = s->ring.capacity / 4; s->opened = true;
    return AUDIO_OK;
}
AudioResult sink_start(AudioSink *s)
{
    if (!owner(s) || atomic_load(&s->invalidated) || atomic_load(&s->state) != SINK_STOPPED) {
        return AUDIO_BAD_STATE;
    }
    atomic_store(&s->media_frames, 0); atomic_store(&s->submitted_frames, 0);
    atomic_store(&s->underrun_frames, 0); s->paused_clock = (SinkClock){0};
    atomic_store(&s->span_read, 0); atomic_store(&s->span_write, 0); atomic_store(&s->span_overflow, 0);
    s->played_media = s->output_base = 0; s->played_valid = false;
    atomic_fetch_or(&s->producer_gate, 1);
    unsigned expected = SINK_STOPPED;
    if (!atomic_compare_exchange_strong(&s->state, &expected, SINK_RUNNING)) {
        atomic_fetch_and(&s->producer_gate, ~1u); return AUDIO_BAD_STATE;
    }
    ++s->clock_epoch;
    AudioResult r = s->backend->start(s->backend_context);
    if (r != AUDIO_OK) { sink_device_lost(s); }
    return r;
}
AudioResult sink_prepare(AudioSink *s)
{
    if (!owner(s) || atomic_load(&s->state) != SINK_STOPPED || atomic_load(&s->invalidated)) {
        return AUDIO_BAD_STATE;
    }
    atomic_store(&s->media_frames, 0); atomic_store(&s->submitted_frames, 0); atomic_store(&s->underrun_frames, 0);
    atomic_store(&s->span_read, 0); atomic_store(&s->span_write, 0); atomic_store(&s->span_overflow, 0);
    s->played_media = s->output_base = 0; s->played_valid = false; s->paused_clock = (SinkClock){0};
    ResetEvent(s->producer_event); atomic_fetch_or(&s->producer_gate, 1); return AUDIO_OK;
}
AudioResult sink_pause(AudioSink *s)
{
    if (!owner(s) || atomic_load(&s->state) != SINK_RUNNING) { return AUDIO_BAD_STATE; }
    SinkInfo frozen = sink_info(s);
    s->paused_clock = frozen.clock;
    unsigned expected = SINK_RUNNING;
    if (!atomic_compare_exchange_strong(&s->state, &expected, SINK_PAUSED)) { return AUDIO_BAD_STATE; }
    AudioResult r = s->backend->stop(s->backend_context);
    if (r != AUDIO_OK) { sink_device_lost(s); }
    return r;
}
AudioResult sink_resume(AudioSink *s)
{
    if (!owner(s) || atomic_load(&s->state) != SINK_PAUSED) { return AUDIO_BAD_STATE; }
    unsigned expected = SINK_PAUSED;
    if (!atomic_compare_exchange_strong(&s->state, &expected, SINK_RUNNING)) { return AUDIO_BAD_STATE; }
    ++s->clock_epoch;
    s->output_base = atomic_load(&s->submitted_frames);
    AudioResult r = s->backend->start(s->backend_context);
    if (r != AUDIO_OK) { sink_device_lost(s); }
    return r;
}
AudioResult sink_stop(AudioSink *s)
{
    if (!owner(s)) { return AUDIO_BAD_STATE; }
    atomic_fetch_and(&s->producer_gate, ~1u);
    SetEvent(s->producer_event);
    unsigned gate;
    while (((gate = atomic_load(&s->producer_gate)) & 2) != 0) {
        WaitOnAddress((volatile void *)&s->producer_gate, &gate, sizeof(gate), INFINITE);
    }
    /* An in-flight arm could have reset the first shutdown signal. */
    SetEvent(s->producer_event);
    atomic_store(&s->state, SINK_STOPPED);
    AudioResult r = s->backend->stop(s->backend_context);
    if (atomic_load(&s->invalidated)) { atomic_store(&s->state, SINK_LOST); }
    atomic_store(&s->wake_armed, 0);
    /* Both endpoints now quiescent; no live reset. */
    (void)spsc_init_pcm(&s->ring, s->pcm, s->pcm_bytes, s->format.channels * sizeof(float));
    return r;
}
AudioResult sink_switch(AudioSink *s)
{
    if (!owner(s)) { return AUDIO_BAD_STATE; }
    const SinkBackend *backend = s->backend; void *ctx = s->backend_context, *pcm = s->pcm;
    size_t pcm_bytes = s->pcm_bytes;
    (void)sink_stop(s);
    s->backend->close(s->backend_context);
    CloseHandle(s->producer_event); CloseHandle(s->control_event); s->opened = false;
    /* Production backend context was owned and released by close. */
    if (backend == &sink_wasapi_backend) { ctx = NULL; }
    return sink_open_with_buffer(s, pcm, pcm_bytes, backend, ctx);
}
AudioResult sink_close(AudioSink *s)
{
    if (!owner(s)) { return AUDIO_BAD_STATE; }
    AudioResult r = sink_stop(s);
    s->backend->close(s->backend_context);
    CloseHandle(s->producer_event); CloseHandle(s->control_event);
    s->producer_event = s->control_event = NULL; s->opened = false;
    return r;
}
AudioResult sink_set_volume(AudioSink *s, float gain)
{
    if (!owner(s)) { return AUDIO_BAD_STATE; }
    if (!isfinite(gain)) { return AUDIO_INVALID; }
    if (gain < 0) { gain = 0; } if (gain > 1) { gain = 1; }
    atomic_store(&s->gain, (unsigned)(gain * 65536.0f)); return AUDIO_OK;
}
SinkInfo sink_info(AudioSink *s)
{
    SinkInfo i = {0}; if (!owner(s)) { return i; }
    i.format = s->format; i.state = (SinkState)atomic_load(&s->state);
    i.clock = i.state == SINK_PAUSED ? s->paused_clock : s->backend->clock(s->backend_context);
    i.media_frames = atomic_load(&s->media_frames); i.submitted_frames = atomic_load(&s->submitted_frames);
    i.underrun_frames = atomic_load(&s->underrun_frames); i.producer_wakes = atomic_load(&s->producer_wakes);
    i.callback_ticks = atomic_load(&s->callback_ticks); i.callback_count = atomic_load(&s->callback_count);
    i.device_bytes = atomic_load(&s->device_bytes); i.device_peak = atomic_load(&s->device_peak);
    i.clock_epoch = s->clock_epoch;
    i.os_buffer_bytes = s->os_buffer_bytes; i.callback_violations = atomic_load(&s->callback_violations);
    sink_timeline_query(s, &i);
    return i;
}
AudioResult sink_write(AudioSink *s, const float *pcm, size_t frames, size_t *written)
{
    if (written) { *written = 0; }
    if (!s || !written || (!pcm && frames)) { return AUDIO_INVALID; }
    if (!producer_enter(s)) { return AUDIO_BAD_STATE; }
    *written = spsc_write(&s->ring, pcm, frames); producer_leave(s);
    return *written == frames ? AUDIO_OK : AUDIO_FULL;
}
bool sink_producer_arm(AudioSink *s)
{
    if (!s || !producer_enter(s)) { return false; }
    ResetEvent(s->producer_event); atomic_store(&s->wake_armed, 1);
    bool wait = s->ring.capacity - spsc_writable(&s->ring) > s->low_water &&
        (atomic_load(&s->producer_gate) & 1) != 0;
    if (!wait) { atomic_store(&s->wake_armed, 0); }
    producer_leave(s); return wait;
}
void sink_device_lost(AudioSink *s)
{
    atomic_store(&s->invalidated, 1);
    atomic_store(&s->state, SINK_LOST); atomic_fetch_and(&s->producer_gate, ~1u);
    SetEvent(s->control_event); SetEvent(s->producer_event);
}
