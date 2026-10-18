#include "sink_test.h"
#include <math.h>

static AudioResult fake_open(void *ctx, AudioSink *s, AudioFormat *format, size_t *os_bytes)
{
    FakeSink *f = ctx; ++f->opens; f->sink = s;
    f->allocation = sink_device_alloc(s, 512); CHECK(f->allocation != NULL);
    *format = f->format; *os_bytes = 4096;
    return f->fail_open ? AUDIO_UNSUPPORTED : AUDIO_OK;
}
static AudioResult fake_start(void *ctx)
{
    FakeSink *f = ctx; ++f->starts;
    return f->fail_start ? AUDIO_BAD_STATE : AUDIO_OK;
}
static AudioResult fake_stop(void *ctx)
{
    FakeSink *f = ctx; ++f->stops;
    return f->fail_stop ? AUDIO_BAD_STATE : AUDIO_OK;
}
static void fake_close(void *ctx)
{
    FakeSink *f = ctx; ++f->closes;
    sink_device_free(f->sink, f->allocation); f->allocation = NULL;
}
static SinkClock fake_clock(void *ctx)
{
    FakeSink *f = ctx; ++f->clocks; return f->clock;
}
const SinkBackend fake_sink_backend = {fake_open, fake_start, fake_stop, fake_close, fake_clock};
static AudioSink sink;
static unsigned char pcm[MEMORY_BUDGET_PCM];
static float data[MEMORY_BUDGET_PCM / sizeof(float)], output[MEMORY_BUDGET_PCM / sizeof(float) + 8];

static void consumption(void)
{
    FakeSink f; fake_init(&f); size_t written;
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_OK);
    CHECK(sink.ring.capacity == 32768 && sink.ring.stride == 8);
    CHECK(sink_start(&sink) == AUDIO_OK);
    const float samples[] = {0.5f,-0.5f,2,-2,NAN,INFINITY};
    CHECK(sink_write(&sink, samples, 3, &written) == AUDIO_OK && written == 3);
    CHECK(sink_set_volume(&sink, 0.5f) == AUDIO_OK);
    size_t bytes = atomic_load(&sink.device_bytes), peak = atomic_load(&sink.device_peak);
    sink_render(&sink, output, 5);
    CHECK(output[0] == 0.25f && output[1] == -0.25f && output[2] == 1 && output[3] == -1);
    for (size_t i = 4; i < 10; ++i) { CHECK(output[i] == 0); }
    SinkInfo i = sink_info(&sink);
    CHECK(i.media_frames == 3 && i.submitted_frames == 5 && i.underrun_frames == 2);
    CHECK(i.clock.valid && i.clock.position == 12 && i.clock.frequency == 48000 && i.clock.padding_frames == 128);
    CHECK(i.clock.latency_100ns == 25000 && i.os_buffer_bytes == 4096);
    CHECK(i.device_bytes == bytes && i.device_peak == peak && i.callback_violations == 0);
    CHECK(sink_set_volume(&sink, NAN) == AUDIO_INVALID);
    CHECK(sink_set_volume(&sink, -1) == AUDIO_OK);
    CHECK(sink_write(&sink, samples, 1, &written) == AUDIO_OK);
    sink_render(&sink, output, 1); CHECK(output[0] == 0 && output[1] == 0);
    CHECK(sink_set_volume(&sink, 99) == AUDIO_OK);
    CHECK(sink_write(&sink, samples + 2, 1, &written) == AUDIO_OK);
    sink_render(&sink, output, 1); CHECK(output[0] == 1 && output[1] == -1);
    CHECK(sink_pause(&sink) == AUDIO_OK);
    uint64_t media = sink_info(&sink).media_frames;
    unsigned clocks = f.clocks; f.clock.position = 999; f.clock.valid = false;
    CHECK(sink_info(&sink).clock.position == 12 && f.clocks == clocks);
    CHECK(sink_write(&sink, samples, 1, &written) == AUDIO_OK);
    sink_render(&sink, output, 1); CHECK(output[0] == 0 && sink_info(&sink).media_frames == media);
    CHECK(sink_resume(&sink) == AUDIO_OK);
    CHECK(sink_info(&sink).clock_epoch == 2);
    CHECK(!sink_info(&sink).clock.valid);
    sink_render(&sink, output, 2);
    CHECK(output[0] == 0.5f && output[1] == -0.5f && output[2] == 0);
    CHECK(sink_info(&sink).media_frames == media + 1);
    CHECK(sink_close(&sink) == AUDIO_OK && atomic_load(&sink.device_bytes) == 0);
    CHECK(!sink.producer_event && !sink.control_event && f.closes == 1);
}
static void wrap_and_wake(void)
{
    FakeSink f; fake_init(&f); size_t written;
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_OK);
    for (size_t i = 0; i < sizeof(data) / sizeof(*data); ++i) { data[i] = (i & 1) ? -0.25f : 0.25f; }
    CHECK(sink_write(&sink, data, sink.ring.capacity + 1, &written) == AUDIO_FULL);
    CHECK(written == sink.ring.capacity);
    CHECK(sink_producer_arm(&sink)); CHECK(sink_start(&sink) == AUDIO_OK);
    sink_render(&sink, output, (uint32_t)(sink.ring.capacity - sink.low_water - 1));
    CHECK(WaitForSingleObject(sink.producer_event, 0) == WAIT_TIMEOUT);
    sink_render(&sink, output, 1);
    CHECK(WaitForSingleObject(sink.producer_event, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(sink.control_event, 0) == WAIT_TIMEOUT);
    sink_render(&sink, output, (uint32_t)sink.low_water + 4);
    sink_render(&sink, output, 4); CHECK(sink_info(&sink).producer_wakes == 1);
    CHECK(!sink_producer_arm(&sink));
    CHECK(sink_write(&sink, data, sink.ring.capacity, &written) == AUDIO_OK);
    CHECK(sink_producer_arm(&sink));
    sink_render(&sink, output, (uint32_t)sink.ring.capacity);
    CHECK(memcmp(data, output, sizeof(data)) == 0 && sink_info(&sink).producer_wakes == 2);
    CHECK(sink_close(&sink) == AUDIO_OK);
}
static DWORD WINAPI wrong_owner(void *ctx)
{
    AudioSink *s = ctx;
    CHECK(sink_start(s) == AUDIO_BAD_STATE && sink_stop(s) == AUDIO_BAD_STATE);
    CHECK(sink_close(s) == AUDIO_BAD_STATE && sink_pause(s) == AUDIO_BAD_STATE);
    CHECK(sink_switch(s) == AUDIO_BAD_STATE && sink_set_volume(s, 1) == AUDIO_BAD_STATE);
    CHECK(sink_info(s).format.sample_rate == 0); return 0;
}
static void lifecycle(void)
{
    FakeSink f; fake_init(&f); size_t written;
    CHECK(sink_open(NULL, pcm, &fake_sink_backend, &f) == AUDIO_INVALID);
    f.fail_open = true;
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_UNSUPPORTED);
    CHECK(f.closes == 1 && !sink.opened && atomic_load(&sink.device_bytes) == 0);
    f.fail_open = false; f.format.channels = 3;
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_UNSUPPORTED);
    f.format.channels = 1; f.format.sample_rate = 44100;
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_OK);
    CHECK(sink.ring.capacity == 65536 && sink_info(&sink).format.sample_rate == 44100);
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_INVALID);
    HANDLE thread = CreateThread(NULL, 0, wrong_owner, &sink, 0, NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread);
    CHECK(sink_pause(&sink) == AUDIO_BAD_STATE && sink_resume(&sink) == AUDIO_BAD_STATE);
    f.fail_start = true; CHECK(sink_start(&sink) == AUDIO_BAD_STATE);
    CHECK(sink_info(&sink).state == SINK_LOST);
    CHECK(sink_write(&sink, data, 1, &written) == AUDIO_BAD_STATE && written == 0);
    CHECK(WaitForSingleObject(sink.control_event, 0) == WAIT_OBJECT_0);
    f.fail_start = false; f.format.sample_rate = 48000;
    CHECK(sink_switch(&sink) == AUDIO_OK && sink_info(&sink).state == SINK_STOPPED);
    CHECK(WaitForSingleObject(sink.control_event, 0) == WAIT_TIMEOUT);
    CHECK(sink_start(&sink) == AUDIO_OK);
    CHECK(sink_write(&sink, data, 7, &written) == AUDIO_OK);
    sink_device_lost(&sink); sink_render(&sink, output, 4);
    CHECK(output[0] == 0 && sink_info(&sink).media_frames == 0);
    CHECK(!sink_producer_arm(&sink));
    CHECK(sink_stop(&sink) == AUDIO_OK);
    CHECK(spsc_readable(&sink.ring) == 0 && !sink_producer_arm(&sink));
    CHECK(sink_start(&sink) == AUDIO_BAD_STATE); /* Loss remains latched until reopen. */
    CHECK(sink_switch(&sink) == AUDIO_OK);
    CHECK(sink_start(&sink) == AUDIO_OK);
    sink_render(&sink, output, 4); CHECK(sink_info(&sink).media_frames == 0);
    f.fail_stop = true; CHECK(sink_pause(&sink) == AUDIO_BAD_STATE);
    CHECK(sink_info(&sink).state == SINK_LOST);
    CHECK(sink_close(&sink) == AUDIO_BAD_STATE && atomic_load(&sink.device_bytes) == 0);
    CHECK(sink_close(&sink) == AUDIO_BAD_STATE);
}
static void allocation_limits(void)
{
    FakeSink f; fake_init(&f);
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_OK);
    CHECK(sink_device_alloc(&sink, SIZE_MAX) == NULL);
    CHECK(sink_device_alloc(&sink, MEMORY_BUDGET_AUDIO_DEVICE) == NULL);
    void *p = sink_device_alloc(&sink, MEMORY_BUDGET_AUDIO_DEVICE - 2048); CHECK(p);
    CHECK(sink_device_alloc(&sink, 4096) == NULL);
    CHECK(sink_info(&sink).device_peak <= MEMORY_BUDGET_AUDIO_DEVICE);
    sink_device_free(&sink, p); CHECK(sink_audit_lock(&sink));
    CHECK(sink_close(&sink) == AUDIO_OK && atomic_load(&sink.device_bytes) == 0);
}
static void handle_lifetime(void)
{
    DWORD before, after; CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    for (unsigned n = 0; n < 64; ++n) {
        FakeSink f; fake_init(&f); f.fail_open = (n & 1) != 0;
        AudioResult result = sink_open(&sink, pcm, &fake_sink_backend, &f);
        CHECK(result == (f.fail_open ? AUDIO_UNSUPPORTED : AUDIO_OK));
        if (result == AUDIO_OK) { CHECK(sink_close(&sink) == AUDIO_OK); }
        CHECK(f.closes == 1 && atomic_load(&sink.device_bytes) == 0);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after)); CHECK(before == after);
}
static DWORD WINAPI run_tests(void *ctx)
{
    (void)ctx;
    consumption(); wrap_and_wake(); lifecycle(); allocation_limits(); handle_lifetime(); sink_race_tests();
    puts("audio sink: passed"); return 0;
}
static DWORD WINAPI gui_owner(void *ctx)
{
    (void)ctx; CHECK(IsGUIThread(TRUE));
    FakeSink f; fake_init(&f);
    CHECK(sink_open(&sink, pcm, &fake_sink_backend, &f) == AUDIO_INVALID);
    CHECK(f.opens == 0 && !sink.opened);
    return 0;
}
int main(void)
{
    /* ASan initializes a message queue on the process main thread. */
    HANDLE thread = CreateThread(NULL, 0, run_tests, NULL, 0, NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread, 80000) == WAIT_OBJECT_0); CloseHandle(thread);
    thread = CreateThread(NULL, 0, gui_owner, NULL, 0, NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread); return 0;
}
