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
    size_t got = spsc_read(&s->ring, output, frames);
    float gain = (float)atomic_load_explicit(&s->gain, memory_order_relaxed) / 65536.0f;
    for (size_t i = 0; i < got * s->format.channels; ++i) {
        float v = output[i] * gain;
        output[i] = !isfinite(v) ? 0 : v > 1 ? 1 : v < -1 ? -1 : v;
    }
    memset((uint8_t *)output + got * stride, 0, ((size_t)frames - got) * stride);
    atomic_fetch_add_explicit(&s->media_frames, got, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->submitted_frames, frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->underrun_frames, frames - got, memory_order_relaxed);
    if (spsc_readable(&s->ring) <= s->low_water && atomic_exchange(&s->wake_armed, 0)) {
        atomic_fetch_add_explicit(&s->producer_wakes, 1, memory_order_relaxed);
        SetEvent(s->producer_event);
    }
    rendering = NULL;
}
