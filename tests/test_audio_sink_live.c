#include "audio/audio_sink.h"
#include "support/test_check.h"
#include <stdlib.h>

static AudioSink sink;
static unsigned char pcm[MEMORY_BUDGET_PCM];
/* Explicit hardware/operator opt-in in addition to CMake registration. */
static DWORD WINAPI run_live(void *ctx)
{
    (void)ctx;
    char enabled[8];
    if (GetEnvironmentVariableA("SPOTIC_TEST_WASAPI", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') {
        puts("Set SPOTIC_TEST_WASAPI=1 and change the default output device during this test."); return 77;
    }
    AudioResult result = sink_open(&sink, pcm, NULL, NULL);
    if (result != AUDIO_OK) { fprintf(stderr, "WASAPI open failed: %d\n", (int)result); return 1; }
    SinkInfo i = sink_info(&sink);
    printf("Native shared format: %u Hz, %u channels, OS buffer %zu bytes, device peak %zu bytes\n",
        i.format.sample_rate, i.format.channels, i.os_buffer_bytes, i.device_peak);
    CHECK(sink_start(&sink) == AUDIO_OK);
    puts("Change the Windows default output device now (60 second deadline)."); fflush(stdout);
    CHECK(WaitForSingleObject(sink.control_event, 60000) == WAIT_OBJECT_0);
    CHECK(sink_info(&sink).state == SINK_LOST);
    CHECK(sink_switch(&sink) == AUDIO_OK);
    i = sink_info(&sink);
    printf("Replacement native format: %u Hz, %u channels\n", i.format.sample_rate, i.format.channels);
    CHECK(sink_start(&sink) == AUDIO_OK);
    CHECK(sink_pause(&sink) == AUDIO_OK && sink_resume(&sink) == AUDIO_OK);
    CHECK(sink_close(&sink) == AUDIO_OK && atomic_load(&sink.device_bytes) == 0);
    return 0;
}
int main(void)
{
    HANDLE thread = CreateThread(NULL, 0, run_live, NULL, 0, NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread, 100000) == WAIT_OBJECT_0);
    DWORD result; CHECK(GetExitCodeThread(thread, &result)); CloseHandle(thread); return (int)result;
}
