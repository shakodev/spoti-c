#include "sink_test.h"

typedef struct Race {
    AudioSink sink;
    unsigned char pcm[MEMORY_BUDGET_PCM];
    FakeSink fake;
    HANDLE step, done;
    _Atomic bool quit;
    _Atomic unsigned rounds, accepted;
} Race;
static Race race;
static DWORD WINAPI drain_thread(void *ctx)
{
    Race *r = ctx; float out[64];
    for (;;) {
        CHECK(WaitForSingleObject(r->step, 10000) == WAIT_OBJECT_0);
        if (atomic_load(&r->quit)) { break; }
        sink_render(&r->sink, out, 32);
        for (size_t i = 0; i < 64; ++i) { CHECK(out[i] == 0.125f); }
        atomic_fetch_add(&r->rounds, 1); SetEvent(r->done);
    }
    return 0;
}
static DWORD WINAPI producer_thread(void *ctx)
{
    Race *r = ctx; float data[2048];
    for (size_t i = 0; i < 2048; ++i) { data[i] = 0.125f; }
    SetEvent(r->step);
    for (;;) {
        size_t written; AudioResult result = sink_write(&r->sink, data, 1024, &written);
        if (result == AUDIO_BAD_STATE) { break; }
        CHECK(result == AUDIO_FULL || result == AUDIO_OK);
        atomic_fetch_add(&r->accepted, (unsigned)written);
        if (result == AUDIO_FULL && sink_producer_arm(&r->sink)) {
            CHECK(WaitForSingleObject(r->sink.producer_event, 10000) == WAIT_OBJECT_0);
        }
    }
    CHECK(!sink_producer_arm(&r->sink));
    size_t written = 7;
    CHECK(sink_write(&r->sink, data, 1, &written) == AUDIO_BAD_STATE && written == 0);
    return 0;
}
void sink_race_tests(void)
{
    Race *r = &race; fake_init(&r->fake);
    atomic_init(&r->quit, false); atomic_init(&r->rounds, 0); atomic_init(&r->accepted, 0);
    CHECK(sink_open(&r->sink, r->pcm, &fake_sink_backend, &r->fake) == AUDIO_OK);
    r->sink.low_water = 8;
    CHECK(sink_start(&r->sink) == AUDIO_OK);
    r->step = CreateEventW(NULL, FALSE, FALSE, NULL);
    r->done = CreateEventW(NULL, FALSE, FALSE, NULL); CHECK(r->step && r->done);
    HANDLE thread = CreateThread(NULL, 0, drain_thread, r, 0, NULL); CHECK(thread);
    float data[64]; for (size_t i = 0; i < 64; ++i) { data[i] = 0.125f; }
    for (unsigned n = 0; n < 10000; ++n) {
        size_t written;
        CHECK(sink_write(&r->sink, data, 32, &written) == AUDIO_OK && written == 32);
        SetEvent(r->step);
        if ((n & 7) == 0) { SwitchToThread(); }
        bool wait = sink_producer_arm(&r->sink);
        CHECK(WaitForSingleObject(r->done, 10000) == WAIT_OBJECT_0);
        if (wait) { CHECK(WaitForSingleObject(r->sink.producer_event, 10000) == WAIT_OBJECT_0); }
        CHECK(WaitForSingleObject(r->sink.control_event, 0) == WAIT_TIMEOUT);
    }
    atomic_store(&r->quit, true); SetEvent(r->step);
    CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0); CloseHandle(thread);
    SinkInfo info = sink_info(&r->sink);
    CHECK(atomic_load(&r->rounds) == 10000 && info.media_frames == 320000 && info.underrun_frames == 0);
    CHECK(info.producer_wakes <= 10000 && info.callback_violations == 0);
    CHECK(sink_stop(&r->sink) == AUDIO_OK);
    for (unsigned n = 0; n < 200; ++n) {
        CHECK(sink_start(&r->sink) == AUDIO_OK);
        thread = CreateThread(NULL, 0, producer_thread, r, 0, NULL); CHECK(thread);
        CHECK(WaitForSingleObject(r->step, 10000) == WAIT_OBJECT_0);
        if ((n & 1) == 0) { SwitchToThread(); }
        CHECK(sink_stop(&r->sink) == AUDIO_OK);
        CHECK(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0); CloseHandle(thread);
        CHECK(spsc_readable(&r->sink.ring) == 0);
    }
    CHECK(sink_close(&r->sink) == AUDIO_OK && atomic_load(&r->sink.device_bytes) == 0);
    CloseHandle(r->step); CloseHandle(r->done);
}
