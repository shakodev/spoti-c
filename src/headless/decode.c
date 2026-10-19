#include "internal.h"

static AudioResult fake_open(void *ctx, AudioSink *sink, AudioFormat *format, size_t *bytes)
{
    (void)ctx; (void)sink; *format = (AudioFormat){48000,2,AUDIO_FLOAT32}; *bytes = 0; return AUDIO_OK;
}
static AudioResult fake_start(void *ctx) { ((Headless *)ctx)->device_frames = 0; return AUDIO_OK; }
static AudioResult fake_stop(void *ctx) { (void)ctx; return AUDIO_OK; }
static void fake_close(void *ctx) { (void)ctx; }
static SinkClock fake_clock(void *ctx)
{
    Headless *h = ctx;
    return (SinkClock){.frequency = 48000, .position = h->device_frames,
        .qpc_100ns = h->device_frames * 10000000 / 48000, .valid = true, .latency_valid = true};
}
static const SinkBackend fake = {fake_open,fake_start,fake_stop,fake_close,fake_clock};
DWORD WINAPI hl_decode(void *ctx)
{
    Headless *h = ctx; crypto_init(&h->crypto);
    bool ok = sink_open_with_buffer(&h->sink,h->pcm,sizeof(h->pcm),h->config.wasapi ? NULL : &fake,h) == AUDIO_OK;
    if (ok) {
        PlayerConfig pc = {.sink = &h->sink, .codec = {&h->arenas[7],&h->arenas[8]}, .work = {&h->arenas[9],&h->arenas[10]}};
        ok = player_create(&h->arenas[0], &pc, &h->player) == PLAYER_OK;
    }
    if (!ok) {
        if (h->sink.opened) { (void)sink_close(&h->sink); }
        fprintf(stderr,"headless startup: decode/device\n"); atomic_store(&h->failed, true); SetEvent(h->decode_ready); return 1;
    }
    SetEvent(h->decode_ready);
    uint64_t next = GetTickCount64();
    PlayerSnapshot previous = {0};
    for (;;) {
        PlayerResult result = player_pump(h->player);
        if (result == PLAYER_CLOSED) { break; }
        if (result != PLAYER_OK && result != PLAYER_MORE) { ok = false; break; }
        if (h->config.state_changed) {
            PlayerSnapshot view = player_snapshot(h->player);
            if (view.state != previous.state || view.generation != previous.generation ||
                view.volume != previous.volume || view.rate != previous.rate ||
                view.duration_frames != previous.duration_frames || view.timeline_valid != previous.timeline_valid ||
                view.error != previous.error || memcmp(view.track_id,previous.track_id,16)) {
                h->config.state_changed(h->config.state_context); previous = view;
            }
        }
        if (!h->config.wasapi && atomic_load(&h->sink.state) == SINK_RUNNING) {
            size_t available = spsc_readable(&h->sink.ring);
            if (available && (!h->config.realtime || GetTickCount64() >= next)) {
                uint32_t n = available > 480 ? 480 : (uint32_t)available;
                float output[960]; LARGE_INTEGER before, after;
                QueryPerformanceCounter(&before); sink_render(&h->sink, output, n); QueryPerformanceCounter(&after);
                atomic_fetch_add(&h->callback_ticks, (uint64_t)(after.QuadPart - before.QuadPart));
                atomic_fetch_add(&h->callbacks, 1); atomic_fetch_add(&h->frames, n); h->device_frames += n;
                if (h->config.observe) { h->config.observe(h->config.observe_context, output, n); }
                next += 10;
                continue;
            }
        }
        SinkInfo info = sink_info(&h->sink); atomic_store(&h->underruns, info.underrun_frames);
        if (info.state != SINK_RUNNING) { next = GetTickCount64(); }
        if (h->config.wasapi) {
            atomic_store(&h->frames, info.media_frames);
            atomic_store(&h->callback_ticks, info.callback_ticks); atomic_store(&h->callbacks, info.callback_count);
        }
        HANDLE handles[3]; size_t n = player_wait_handles(h->player, handles);
        DWORD wait = player_next_wait_ms(h->player);
        if (!h->config.wasapi) {
            /* Fake callbacks share this owner. Do not wait on the producer's
             * low-water event when waiting for network input. */
            n = n > 2 ? 2 : n;
            if (atomic_load(&h->sink.state) == SINK_RUNNING && spsc_readable(&h->sink.ring)) {
                if (wait > 10) { wait = h->config.realtime ? 10 : 0; }
            }
        }
        if (WaitForMultipleObjects((DWORD)n, handles, FALSE, wait) == WAIT_FAILED) { ok = false; break; }
        atomic_fetch_add(&h->wakes, 1);
    }
    atomic_store_explicit(&h->decode_finished, true, memory_order_release); SetEvent(h->read_wake);
    (void)WaitForSingleObject(h->decode_ready, INFINITE);
    AcquireSRWLockExclusive(&h->lifecycle);
    ok &= player_destroy(h->player) == PLAYER_OK;
    ReleaseSRWLockExclusive(&h->lifecycle);
    ok &= sink_close(&h->sink) == AUDIO_OK && crypto_balanced(&h->crypto);
    if (!ok) { atomic_store(&h->failed, true); }
    SetEvent(h->read_wake); return ok ? 0 : 1;
}
