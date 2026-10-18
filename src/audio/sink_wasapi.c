#define COBJMACROS
#include "audio_sink.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wchar.h>
#include <string.h>
#include "miniaudio_config.h"
#include "miniaudio.h"

typedef struct WasapiSink {
    IMMNotificationClient notification;
    LONG refs;
    AudioSink *sink;
    ma_context context;
    ma_device device;
    IMMDeviceEnumerator *enumerator;
    IAudioClock *clock;
    bool context_ready, device_ready, com_ready, registered;
} WasapiSink;
_Static_assert(sizeof(WasapiSink) + 16 < MEMORY_BUDGET_AUDIO_DEVICE, "WASAPI device cap");

extern size_t sink_device_allocation_size(const void *);
static WasapiSink *from_notification(IMMNotificationClient *n)
{
    return (WasapiSink *)((char *)n - offsetof(WasapiSink, notification));
}
static HRESULT STDMETHODCALLTYPE notify_query(IMMNotificationClient *n, REFIID iid, void **out)
{
    static const IID unknown = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
    static const IID notification = {0x7991eec9,0x7e89,0x4d85,{0x83,0x90,0x6c,0x70,0x3c,0xec,0x60,0xc0}};
    if (!out) { return E_POINTER; } *out = NULL;
    if (!IsEqualIID(iid, &unknown) && !IsEqualIID(iid, &notification)) { return E_NOINTERFACE; }
    *out = n; InterlockedIncrement(&from_notification(n)->refs); return S_OK;
}
static ULONG STDMETHODCALLTYPE notify_addref(IMMNotificationClient *n)
{
    return (ULONG)InterlockedIncrement(&from_notification(n)->refs);
}
static ULONG STDMETHODCALLTYPE notify_release(IMMNotificationClient *n)
{
    return (ULONG)InterlockedDecrement(&from_notification(n)->refs);
}
static HRESULT STDMETHODCALLTYPE notify_state(IMMNotificationClient *n, LPCWSTR id, DWORD state)
{
    (void)n; (void)id; (void)state; return S_OK;
}
static HRESULT STDMETHODCALLTYPE notify_added(IMMNotificationClient *n, LPCWSTR id)
{
    (void)n; (void)id; return S_OK;
}
static HRESULT STDMETHODCALLTYPE notify_default(IMMNotificationClient *n, EDataFlow flow, ERole role, LPCWSTR id)
{
    (void)id;
    if (flow == eRender && role == eConsole) { sink_device_lost(from_notification(n)->sink); }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE notify_property(IMMNotificationClient *n, LPCWSTR id, const PROPERTYKEY key)
{
    (void)n; (void)id; (void)key; return S_OK;
}
static IMMNotificationClientVtbl notify_vtable = {
    notify_query, notify_addref, notify_release, notify_state, notify_added,
    notify_added, notify_default, notify_property
};
static void *device_malloc(size_t n, void *ctx) { return sink_device_alloc(ctx, n); }
static void device_free(void *p, void *ctx) { sink_device_free(ctx, p); }
static void *device_realloc(void *p, size_t n, void *ctx)
{
    if (!p) { return device_malloc(n, ctx); }
    if (!n) { device_free(p, ctx); return NULL; }
    void *next = device_malloc(n, ctx);
    if (next) {
        size_t old = sink_device_allocation_size(p);
        memcpy(next, p, old < n ? old : n); device_free(p, ctx);
    }
    return next;
}
static void render(ma_device *d, void *out, const void *in, ma_uint32 frames)
{
    (void)in; WasapiSink *w = d->pUserData;
    LARGE_INTEGER before, after; QueryPerformanceCounter(&before);
    sink_render(w->sink, out, frames);
    QueryPerformanceCounter(&after);
    atomic_fetch_add_explicit(&w->sink->callback_ticks, (uint64_t)(after.QuadPart - before.QuadPart), memory_order_relaxed);
    atomic_fetch_add_explicit(&w->sink->callback_count, 1, memory_order_relaxed);
}
static void notification(const ma_device_notification *n)
{
    WasapiSink *w = n->pDevice->pUserData;
    if (n->type == ma_device_notification_type_stopped &&
        atomic_load(&w->sink->state) == SINK_RUNNING) { sink_device_lost(w->sink); }
}
static void close_device(void *ctx)
{
    WasapiSink *w = ctx; if (!w) { return; }
    if (w->registered) {
        IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(w->enumerator, &w->notification);
    }
    if (w->clock) { IAudioClock_Release(w->clock); }
    if (w->device_ready) { ma_device_uninit(&w->device); }
    if (w->context_ready) { ma_context_uninit(&w->context); }
    if (w->enumerator) { IMMDeviceEnumerator_Release(w->enumerator); }
    if (w->com_ready) { CoUninitialize(); }
    AudioSink *s = w->sink; sink_device_free(s, w); s->backend_context = NULL;
}
static AudioResult open_device(void *ctx, AudioSink *s, AudioFormat *format, size_t *os_bytes)
{
    (void)ctx;
    static const CLSID enumerator_class = {0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};
    static const IID enumerator_iid = {0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
    static const IID clock_iid = {0xcd63314f,0x3fba,0x4a1b,{0x81,0x2c,0xef,0x96,0x35,0x87,0x28,0xe7}};
    WasapiSink *w = sink_device_alloc(s, sizeof(*w));
    if (!w) { return AUDIO_NO_MEMORY; }
    memset(w, 0, sizeof(*w)); w->sink = s; s->backend_context = w;
    w->notification.lpVtbl = &notify_vtable; w->refs = 1;
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { return AUDIO_BAD_STATE; } w->com_ready = true;
    ma_context_config cc = ma_context_config_init();
    cc.allocationCallbacks = (ma_allocation_callbacks){s, device_malloc, device_realloc, device_free};
    ma_backend backend = ma_backend_wasapi;
    ma_result mr = ma_context_init(&backend, 1, &cc, &w->context);
    if (mr != MA_SUCCESS) { return mr == MA_OUT_OF_MEMORY ? AUDIO_NO_MEMORY : AUDIO_UNSUPPORTED; }
    w->context_ready = true;
    ma_device_config dc = ma_device_config_init(ma_device_type_playback);
    dc.playback.format = ma_format_f32; dc.playback.channels = 0;
    dc.playback.shareMode = ma_share_mode_shared; dc.sampleRate = 0;
    dc.performanceProfile = ma_performance_profile_low_latency;
    dc.noPreSilencedOutputBuffer = MA_TRUE; dc.noClip = MA_TRUE; dc.noFixedSizedCallback = MA_TRUE;
    dc.wasapi.noAutoConvertSRC = MA_TRUE; dc.wasapi.noDefaultQualitySRC = MA_TRUE;
    dc.wasapi.noAutoStreamRouting = MA_TRUE; dc.wasapi.noHardwareOffloading = MA_TRUE;
    dc.dataCallback = render; dc.notificationCallback = notification; dc.pUserData = w;
    mr = ma_device_init(&w->context, &dc, &w->device);
    if (mr != MA_SUCCESS) { return mr == MA_OUT_OF_MEMORY ? AUDIO_NO_MEMORY : AUDIO_UNSUPPORTED; }
    w->device_ready = true;
    ma_device *d = &w->device;
    if (d->playback.internalFormat != ma_format_f32 || d->sampleRate != d->playback.internalSampleRate ||
        d->playback.channels != d->playback.internalChannels) { return AUDIO_UNSUPPORTED; }
    *format = (AudioFormat){d->sampleRate, d->playback.channels, AUDIO_FLOAT32};
    *os_bytes = (size_t)d->wasapi.actualBufferSizeInFramesPlayback * d->playback.internalChannels * sizeof(float);
    IAudioClient *client = d->wasapi.pAudioClientPlayback;
    hr = IAudioClient_GetService(client, &clock_iid, (void **)&w->clock);
    if (FAILED(hr)) { return AUDIO_UNSUPPORTED; }
    hr = CoCreateInstance(&enumerator_class, NULL, CLSCTX_ALL, &enumerator_iid, (void **)&w->enumerator);
    if (FAILED(hr)) { return AUDIO_UNSUPPORTED; }
    hr = IMMDeviceEnumerator_RegisterEndpointNotificationCallback(w->enumerator, &w->notification);
    if (FAILED(hr)) { return AUDIO_UNSUPPORTED; } w->registered = true;
    return AUDIO_OK;
}
static AudioResult start_device(void *ctx)
{
    WasapiSink *w = ctx;
    return ma_device_start(&w->device) == MA_SUCCESS ? AUDIO_OK : AUDIO_BAD_STATE;
}
static AudioResult stop_device(void *ctx)
{
    WasapiSink *w = ctx;
    return ma_device_stop(&w->device) == MA_SUCCESS ? AUDIO_OK : AUDIO_BAD_STATE;
}
static SinkClock clock_device(void *ctx)
{
    WasapiSink *w = ctx; SinkClock c = {0}; REFERENCE_TIME latency = 0;
    IAudioClient *client = w->device.wasapi.pAudioClientPlayback;
    c.valid = IAudioClock_GetPosition(w->clock, &c.position, &c.qpc_100ns) == S_OK &&
        SUCCEEDED(IAudioClock_GetFrequency(w->clock, &c.frequency)) && c.frequency != 0;
    c.latency_valid = SUCCEEDED(IAudioClient_GetCurrentPadding(client, &c.padding_frames)) &&
        SUCCEEDED(IAudioClient_GetStreamLatency(client, &latency));
    if (c.latency_valid) { c.latency_100ns = (uint64_t)latency; }
    return c;
}
const SinkBackend sink_wasapi_backend = {open_device, start_device, stop_device, close_device, clock_device};
