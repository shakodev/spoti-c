#ifndef SPOTIC_CORE_WORKER_H
#define SPOTIC_CORE_WORKER_H

#include "clock.h"
#include "event_queue.h"
#include <stdatomic.h>

#define WORKER_MAX_JOBS ((size_t)128)

typedef struct CancelToken {
    _Atomic bool requested;
    HANDLE wake; /* Borrowed active-job cancellation event. */
    CoreClock clock;
    uint64_t deadline;
} CancelToken;

bool worker_cancelled(const CancelToken *token);
uint32_t worker_cancel_wait(const CancelToken *token);
HANDLE worker_cancel_handle(const CancelToken *token);

typedef enum WorkerOutcome { WORKER_DONE, WORKER_CANCELLED, WORKER_EXPIRED } WorkerOutcome;
typedef enum WorkerPriority { WORKER_DISK, WORKER_ART } WorkerPriority;

typedef struct WorkerJob {
    Event request;
    void (*run)(const Event *request, const CancelToken *cancel, void *context);
    void (*complete)(const Event *request, WorkerOutcome outcome, void *context);
    void *context;
    uint64_t not_before, deadline; /* Absolute clock ms; deadline may be NEVER. */
    WorkerPriority priority;
} WorkerJob;

typedef struct WorkerSlot {
    WorkerJob job;
    CancelToken cancel;
    uint64_t ticket;
    unsigned state;
} WorkerSlot;

typedef struct WorkerStats {
    uint64_t accepted, completed, waits;
    size_t pending;
    DWORD error;
    bool stopping;
} WorkerStats;

typedef struct Worker {
    SRWLOCK lock;
    HANDLE wake, cancel, idle, thread;
    Metrics *metrics;
    MetricsHandle wake_metric, cancel_metric, idle_metric, thread_metric;
    WorkerSlot *slots;
    size_t capacity;
    CoreClock clock;
    WorkerStats stats;
} Worker;

/* One blocking art/cache worker, fixed slots, no generic thread pool. Uses the
 * SAME setup-owned <=128 KiB core arena as event queues and future logs.
 * All callbacks run outside the lock, exactly one completion per accepted job.
 * Callback/context/request resources stay alive through completion. No borrowed
 * cancel token may survive its callback. Disk jobs precede ready art jobs;
 * FIFO by ticket within each priority. A running callback is not preempted. */
CoreResult worker_start(Arena *core, size_t capacity, CoreClock clock, Worker **output);
CoreResult worker_submit(Worker *worker, const WorkerJob *job, uint64_t *ticket);
bool worker_cancel(Worker *worker, uint64_t ticket);
/* Callback cancellation is cooperative: poll token or wait on its handle with
 * worker_cancel_wait as timeout to honor deadlines. Blocking OS I/O must support
 * cancellation/bounded waits. Expired/queued-cancelled jobs skip run, still complete. */
CoreResult worker_stop(Worker *worker);
/* Stop admission, stop/join external submitters, then join from the owner thread.
 * Never join from a callback. Timeout keeps storage/handles live for a retry;
 * successful join closes all worker handles. Destroy destination queues AFTER
 * completions finish. No forced thread termination or live arena reset. */
CoreResult worker_join(Worker *worker, DWORD timeout_ms);
/* Borrowed idle handle signals no pending/running jobs; not a shutdown barrier.
 * Never ResetEvent/CloseHandle any borrowed worker or cancellation handle. */
HANDLE worker_idle_handle(const Worker *worker);
WorkerStats worker_stats(Worker *worker);
/* Wake the scheduler after changing an injected clock. This does not interrupt
 * an active callback's I/O/wait. Real clocks use nearest-deadline waits. */
CoreResult worker_notify_clock(Worker *worker);

#endif
