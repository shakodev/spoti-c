#include "audio_sink.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static _Thread_local AudioSink *rendering;
typedef union DeviceHeader {
    _Alignas(16) unsigned char alignment[16];
    struct { size_t bytes; } size;
} DeviceHeader;

bool sink_audit_lock(AudioSink *s)
{
    if (rendering == s) { atomic_fetch_add(&s->callback_violations, 1); return false; }
    return true;
}
void *sink_device_alloc(AudioSink *s, size_t bytes)
{
    if (rendering == s) { atomic_fetch_add(&s->callback_violations, 1); return NULL; }
    if (bytes == 0 || bytes > MEMORY_BUDGET_AUDIO_DEVICE - sizeof(DeviceHeader)) { return NULL; }
    size_t total = bytes + sizeof(DeviceHeader), current = atomic_load(&s->device_bytes);
    do {
        if (current > MEMORY_BUDGET_AUDIO_DEVICE - total) { return NULL; }
    } while (!atomic_compare_exchange_weak(&s->device_bytes, &current, current + total));
    DeviceHeader *h = malloc(total);
    if (!h) { atomic_fetch_sub(&s->device_bytes, total); return NULL; }
    h->size.bytes = total;
    size_t peak = atomic_load(&s->device_peak);
    while (peak < current + total &&
        !atomic_compare_exchange_weak(&s->device_peak, &peak, current + total)) { }
    return h + 1;
}
void sink_device_free(AudioSink *s, void *p)
{
    if (!p) { return; }
    if (rendering == s) { atomic_fetch_add(&s->callback_violations, 1); return; }
    DeviceHeader *h = (DeviceHeader *)p - 1;
    atomic_fetch_sub(&s->device_bytes, h->size.bytes); free(h);
}
/* Private to the miniaudio adapter, pointer originates from the allocator. */
size_t sink_device_allocation_size(const void *p)
{
    const DeviceHeader *h = (const DeviceHeader *)p - 1;
    return h->size.bytes - sizeof(DeviceHeader);
}
void sink_render(AudioSink *s, float *output, uint32_t frames)
{
    if (!s || !output || frames == 0) { return; }
    rendering = s;
    size_t stride = s->format.channels * sizeof(float);
    if (atomic_load_explicit(&s->state, memory_order_acquire) != SINK_RUNNING) {
        memset(output, 0, (size_t)frames * stride); rendering = NULL; return;
    }
    uint64_t before = audio_diagnostic_now(s->diagnostics);
    if (s->diagnostics) {
        audio_diagnostic_record(s->diagnostics, AD_CALLBACK_FRAMES, frames, before);
        if (s->diagnostic_callback_qpc) {
            audio_diagnostic_record(s->diagnostics, AD_CALLBACK_GAP_TICKS, before - s->diagnostic_callback_qpc, before);
        }
        s->diagnostic_callback_qpc = before;
    }
    size_t got = spsc_read(&s->ring, output, frames);
    float gain = (float)atomic_load_explicit(&s->gain, memory_order_relaxed) / 65536.0f;
    for (size_t i = 0; i < got * s->format.channels; ++i) {
        float v = output[i] * gain;
        output[i] = !isfinite(v) ? 0 : v > 1 ? 1 : v < -1 ? -1 : v;
    }
    memset((uint8_t *)output + got * stride, 0, ((size_t)frames - got) * stride);
    uint64_t media = atomic_fetch_add_explicit(&s->media_frames, got, memory_order_relaxed);
    uint64_t submitted = atomic_fetch_add_explicit(&s->submitted_frames, frames, memory_order_relaxed);
    sink_timeline_record(s, submitted, media, frames, (uint32_t)got);
    atomic_fetch_add_explicit(&s->underrun_frames, frames - got, memory_order_relaxed);
    size_t remaining = spsc_readable(&s->ring);
    if (s->diagnostics) {
        if (got < frames) { audio_diagnostic_record(s->diagnostics, AD_UNDERRUN_FRAMES, frames - got, before); }
        if (remaining <= s->low_water) { audio_diagnostic_record(s->diagnostics, AD_LOW_WATER_FRAMES, remaining, before); }
    }
    if (remaining <= s->low_water && atomic_exchange(&s->wake_armed, 0)) {
        if (s->diagnostics) { atomic_store_explicit(&s->diagnostic_wake_qpc, before, memory_order_release); }
        atomic_fetch_add_explicit(&s->producer_wakes, 1, memory_order_relaxed);
        SetEvent(s->producer_event);
    }
    audio_diagnostic_elapsed(s->diagnostics, AD_CALLBACK_TICKS, before);
    rendering = NULL;
}
