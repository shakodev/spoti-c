#include "headless_support.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static void identity(FILE *file)
{
    typedef LONG (WINAPI *Version)(OSVERSIONINFOW *);
    FARPROC address = GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"); Version version;
    CHECK(address); memcpy(&version, &address, sizeof(version));
    OSVERSIONINFOW info = {0}; info.dwOSVersionInfoSize = sizeof(info); CHECK(version(&info) == 0);
    SYSTEM_INFO system; GetNativeSystemInfo(&system);
    char cpu[256]; DWORD count = GetEnvironmentVariableA("PROCESSOR_IDENTIFIER", cpu, sizeof(cpu));
    CHECK(count && count < sizeof(cpu));
    for (DWORD i = 0; i < count; ++i) { if (cpu[i] == '"' || cpu[i] == '\\' || (unsigned char)cpu[i] < 32) { cpu[i] = '_'; } }
    fprintf(file,"{\"schema\":1,\"os\":\"Windows %lu.%lu build %lu\",\"cpu\":\"%s\",\"architecture\":%u,"
        "\"processors\":%lu,\"device\":\"deterministic fake 48000Hz stereo float32\",\"duration_seconds\":600,"
        "\"nominal_bitrate\":320000,\"fixture_measured_bitrate\":362806.3741783638,"
        "\"resident_limit_bytes\":8000000,\"asan\":false,\"working_set_trimming\":false}\n",
        info.dwMajorVersion,info.dwMinorVersion,info.dwBuildNumber,cpu,(unsigned)system.wProcessorArchitecture,system.dwNumberOfProcessors);
}
int main(int argc, char **argv)
{
#ifdef SPOTIC_ASAN
    (void)argc; (void)argv; fputs("Resident benchmark cannot run under ASan.\n", stderr); return 1;
#else
    FILE *raw = NULL; CHECK(fopen_s(&raw, argc > 1 ? argv[1] : HEADLESS_OUTPUT_DIR "/headless-metrics.jsonl", "w") == 0);
    identity(raw); fflush(raw);
    HeadlessFixture fixture; hf_open(&fixture);
    HeadlessConfig c = {.cache_directory = fixture.cache, .fixture_url = fixture.url, .realtime = true}; Headless *h;
    CHECK(headless_open(&c, &h)); hf_load(h, false);
    PlayerCommand command = {.type = PLAYER_QUEUE_APPEND}; command.track_id[15] = 2;
    CHECK(headless_submit(h,&command) == PLAYER_OK);
    command = (PlayerCommand){.type = PLAYER_REPEAT, .value = PLAYER_REPEAT_ALL};
    CHECK(headless_submit(h,&command) == PLAYER_OK && headless_command(h,"play"));
    uint64_t start = GetTickCount64(), next_sample = start, next_seek = start + 45000, peak = 0;
    bool healthy = true; unsigned seeks = 0;
    while (GetTickCount64() - start < 600000) {
        uint64_t now = GetTickCount64(); PlayerSnapshot state = headless_snapshot(h);
        if (state.state == PLAYER_ERROR) { healthy = false; break; }
        if (now >= next_seek) { healthy &= headless_command(h,"seek 1500"); ++seeks; next_seek += 45000; }
        if (now >= next_sample) {
            HeadlessMetrics m; CHECK(headless_metrics(h,&m) && headless_write_metrics(raw,&m)); fflush(raw);
            if (m.peak_working_set > peak) { peak = m.peak_working_set; } next_sample += 1000;
        }
        Sleep(25);
    }
    HeadlessMetrics m; healthy &= headless_close(h,&m) && m.balanced;
    hf_close(&fixture);
    CHECK(headless_process_metrics(&m) && headless_write_metrics(raw,&m));
    if (m.peak_working_set > peak) { peak = m.peak_working_set; }
    healthy &= seeks == 13 && m.frames >= UINT64_C(48000) * 590 && m.underruns == 0;
    bool budget = peak < 8000000;
    fprintf(raw,"{\"elapsed_ms\":%" PRIu64 ",\"seeks\":%u,\"healthy\":%s,\"resident_budget_pass\":%s,\"live_acceptance\":\"skipped\"}\n",
        GetTickCount64()-start,seeks,healthy ? "true" : "false",budget ? "true" : "false");
    CHECK(fclose(raw) == 0);
    printf("600-second headless benchmark: peak resident working set=%" PRIu64 " limit=8000000 healthy=%u\n",peak,healthy);
    return healthy && budget ? 0 : 1;
#endif
}
