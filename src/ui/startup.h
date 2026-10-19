#ifndef SPOTIC_STARTUP_H
#define SPOTIC_STARTUP_H
#include "app.h"
#include "core/state_cache.h"
#include "core/worker.h"

enum { STARTUP_EPOCH=1 };
typedef enum StartupMilestone { STARTUP_PROCESS, STARTUP_MAIN, STARTUP_WINDOW,
    STARTUP_PRESENT, STARTUP_SERVICES, STARTUP_ONLINE, STARTUP_MILESTONES } StartupMilestone;
typedef struct StartupTimes {
    uint64_t frequency, ticks[STARTUP_MILESTONES];
    uint32_t cache_result, useful;
} StartupTimes;
typedef struct Startup {
    StateCache restored, pending, captured;
    StartupTimes times;
    wchar_t path[240];
    Worker *worker;
    SRWLOCK lock;
    StateCacheResult save_result;
    bool busy, services_started;
} Startup;
_Static_assert(sizeof(Startup)<=MEMORY_BUDGET_STARTUP,"bounded startup snapshot and worker handoff storage");
void startup_begin(Startup *, const wchar_t *cache_path);
void startup_mark(StartupTimes *, StartupMilestone);
double startup_ms(const StartupTimes *, StartupMilestone);
void startup_restore(App *, const StateCache *);
void startup_cached_view(App *); /* Layout only, borrowed snapshot outlives App. */
void startup_capture(StateCache *, const App *, HWND);
void startup_place(HWND, const StateCache *);
StateCacheResult startup_writer_start(Startup *, Arena *);
StateCacheResult startup_save(Startup *, const StateCache *);
StateCacheResult startup_writer_close(Startup *, DWORD timeout);
StateCacheResult startup_save_status(Startup *, bool *busy);
#endif
