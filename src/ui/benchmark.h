#ifndef SPOTIC_UI_BENCHMARK_H
#define SPOTIC_UI_BENCHMARK_H
#include "app_desktop.h"
#include "headless/headless.h"

#define BENCH_MESSAGE (WM_APP+135)
#define BENCH_VERSION 1u
typedef enum BenchCommand {
    BENCH_SNAPSHOT, BENCH_LOAD, BENCH_PLAY, BENCH_PAUSE, BENCH_SEEK,
    BENCH_SCROLL, BENCH_DEVICE_LOSS
} BenchCommand;
/* Same-build, same-user diagnostic ABI, used only with --benchmark=<mapping>.
 * Synchronous SendMessageTimeout acknowledges publication. No pointers cross
 * processes and no polling timer runs in the measured application. */
typedef struct BenchSnapshot {
    uint32_t version, ready, balanced, player_state, player_error;
    uint64_t input_messages;
    StartupTimes startup;
    UiWindowStats window;
    UiRendererStats renderer;
    MetricsSnapshot accounting;
    HeadlessMetrics audio;
    uint64_t gpu_local, gpu_nonlocal, driver_version;
    uint32_t gpu_valid, vendor, device;
    wchar_t adapter[128];
} BenchSnapshot;
typedef struct BenchProbe {
    HANDLE mapping;
    BenchSnapshot *shared;
    Metrics metrics;
    MetricsArena binding;
    uint64_t input_messages;
} BenchProbe;
bool bench_probe_open(BenchProbe *, const wchar_t *name);
void bench_probe_close(BenchProbe *, bool balanced);
bool bench_probe_input(BenchProbe *, AppDesktop *, UiWindow *, Headless *, UINT, WPARAM, LRESULT *);
#endif
