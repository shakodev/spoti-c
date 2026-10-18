#include "internal.h"
#include <psapi.h>
#include <inttypes.h>
bool headless_process_metrics(HeadlessMetrics *out)
{
    if (!out) { return false; }
    PROCESS_MEMORY_COUNTERS_EX2 counters = {0}; counters.cb = sizeof(counters);
    DWORD handles = 0;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&counters, sizeof(counters)) ||
        !GetProcessHandleCount(GetCurrentProcess(), &handles)) { return false; }
    out->working_set = counters.WorkingSetSize; out->peak_working_set = counters.PeakWorkingSetSize;
    out->private_working_set = counters.PrivateWorkingSetSize; out->private_commit = counters.PrivateUsage;
    out->process_handles = handles; return true;
}
bool headless_metrics(Headless *h, HeadlessMetrics *out)
{
    if (!h || !out) { return false; }
    *out = (HeadlessMetrics){0};
    if (!headless_process_metrics(out)) { return false; }
    out->network_attempts = atomic_load(&h->network_attempts); out->cache_hits = atomic_load(&h->cache_hits);
    out->arena_high_water = metrics_snapshot(&h->accounting).allocation_high_water;
    out->wakeups = atomic_load(&h->wakes); out->callback_ticks = atomic_load(&h->callback_ticks);
    out->callbacks = atomic_load(&h->callbacks); out->frames = atomic_load(&h->frames);
    out->underruns = atomic_load(&h->underruns); out->qpc_frequency = (uint64_t)h->frequency.QuadPart;
    return true;
}
bool headless_write_metrics(FILE *file, const HeadlessMetrics *m)
{
    if (!file || !m) { return false; }
    return fprintf(file, "{\"working_set\":%" PRIu64 ",\"peak_working_set\":%" PRIu64
        ",\"private_working_set\":%" PRIu64 ",\"private_commit\":%" PRIu64
        ",\"arena_high_water\":%" PRIu64 ",\"wakeups\":%" PRIu64
        ",\"callback_ticks\":%" PRIu64 ",\"callbacks\":%" PRIu64 ",\"qpc_frequency\":%" PRIu64
        ",\"frames\":%" PRIu64 ",\"underruns\":%" PRIu64 ",\"network_attempts\":%" PRIu64
        ",\"cache_hits\":%" PRIu64 ",\"process_handles\":%u,\"balanced\":%s}\n",
        m->working_set,m->peak_working_set,m->private_working_set,m->private_commit,m->arena_high_water,
        m->wakeups,m->callback_ticks,m->callbacks,m->qpc_frequency,m->frames,m->underruns,
        m->network_attempts,m->cache_hits,m->process_handles,m->balanced ? "true" : "false") > 0;
}
