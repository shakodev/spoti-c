#include "core_event_test.h"

typedef struct JobContext {
    EventQueue *completed;
    HANDLE started, release;
    _Atomic uint32_t runs;
} JobContext;

static uint64_t fake_now(void *context)
{
    return atomic_load((_Atomic uint64_t *)context);
}

static void run_simple(const Event *request, const CancelToken *token, void *context)
{
    JobContext *job = context;
    (void)request;
    test_heap_watch(true);
    CHECK(!worker_cancelled(token));
    atomic_fetch_add(&job->runs, 1);
    test_heap_watch(false);
}

static void run_until_released(const Event *request, const CancelToken *token, void *context)
{
    JobContext *job = context;
    (void)request;
    CHECK(worker_cancel_handle(token) != NULL);
    atomic_fetch_add(&job->runs, 1);
    CHECK(SetEvent(job->started));
    /* Deliberately delay cancellation acknowledgement to verify join timeout
     * retains live resources. The fixture has its own bounded external release. */
    CHECK(WaitForSingleObject(job->release, 5000) == WAIT_OBJECT_0);
}

static void run_until_cancelled(const Event *request, const CancelToken *token, void *context)
{
    JobContext *job = context;
    (void)request;
    test_heap_watch(true);
    CHECK(!worker_cancelled(token) && worker_cancel_wait(token) == INFINITE);
    atomic_fetch_add(&job->runs, 1);
    CHECK(SetEvent(job->started));
    CHECK(WaitForSingleObject(worker_cancel_handle(token), 5000) == WAIT_OBJECT_0);
    CHECK(worker_cancelled(token) && worker_cancel_wait(token) == 0);
    test_heap_watch(false);
}

static void complete(const Event *request, WorkerOutcome outcome, void *context)
{
    JobContext *job = context;
    Event event = *request;
    event.payload[0] = outcome;
    test_heap_watch(true);
    CHECK(event_post(job->completed, &event) == CORE_OK);
    test_heap_watch(false);
}

static WorkerJob job_description(JobContext *context, uint64_t id)
{
    return (WorkerJob){.request = {.type = 99, .generation = 7, .id = id},
        .run = run_simple, .complete = complete, .context = context,
        .not_before = 0, .deadline = CORE_CLOCK_NEVER, .priority = WORKER_ART};
}

static Event take(EventQueue *queue)
{
    Event event;
    size_t count;
    CHECK(WaitForSingleObject(event_wait_handle(queue), 5000) == WAIT_OBJECT_0);
    CHECK(event_drain(queue, &event, 1, &count) == CORE_OK && count == 1);
    CHECK(event.type == 99 && event.generation == 7);
    return event;
}

static void assert_closed(Worker *worker)
{
    WorkerStats stats = worker_stats(worker);
    CHECK(stats.stopping && stats.pending == 0 && stats.accepted == stats.completed);
    CHECK(stats.error == ERROR_SUCCESS);
    CHECK(worker->thread == NULL && worker->wake == NULL && worker->cancel == NULL);
    CHECK(worker_idle_handle(worker) == NULL);
    CHECK(worker_join(worker, 0) == CORE_CLOSED);
    CHECK(worker_stop(worker) == CORE_CLOSED && worker_notify_clock(worker) == CORE_CLOSED);
}

static void finish_queue(EventQueue *queue)
{
    size_t count;
    CHECK(event_queue_close(queue) == CORE_OK);
    CHECK(event_drain(queue, NULL, 0, &count) == CORE_CLOSED && count == 0);
    CHECK(event_queue_destroy(queue) == CORE_OK);
}

static void test_shutdown(void)
{
    _Alignas(64) uint8_t memory[MEMORY_BUDGET_CORE];
    Arena core;
    Worker *worker;
    EventQueue *queue;
    uint64_t ticket[5];
    JobContext context = {0};
    CHECK(arena_init(&core, memory, sizeof(memory)));
    CHECK(event_queue_create(&core, 8, &queue) == CORE_OK);
    context.completed = queue;
    context.started = CreateEventW(NULL, TRUE, FALSE, NULL);
    context.release = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(context.started != NULL && context.release != NULL);
    test_heap_watch(false);
    CHECK(worker_start(&core, 4, (CoreClock){0}, &worker) == CORE_OK);
    test_heap_watch(true);
    CHECK(worker_join(worker, 0) == CORE_INVALID);
    /* Empty worker enters one infinite wait; no periodic idle polling. */
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    WorkerStats before = worker_stats(worker);
    CHECK(before.waits != 0);
    Sleep(30);
    CHECK(worker_stats(worker).waits == before.waits);
    WorkerJob job = job_description(&context, 1);
    job.run = run_until_released;
    CHECK(worker_submit(worker, &job, &ticket[0]) == CORE_OK);
    CHECK(WaitForSingleObject(context.started, 5000) == WAIT_OBJECT_0);
    job = job_description(&context, 2);
    CHECK(worker_submit(worker, &job, &ticket[1]) == CORE_OK);
    job.request.id = 3;
    job.priority = WORKER_DISK;
    CHECK(worker_submit(worker, &job, &ticket[2]) == CORE_OK);
    job.request.id = 4;
    job.not_before = CORE_CLOCK_NEVER;
    CHECK(worker_submit(worker, &job, &ticket[3]) == CORE_OK);
    CHECK(worker_submit(worker, &job, &ticket[4]) == CORE_FULL && ticket[4] == 0);
    CHECK(worker_cancel(worker, ticket[1]));
    CHECK(!worker_cancel(worker, UINT64_MAX));
    CHECK(worker_stop(worker) == CORE_OK);
    CHECK(worker_submit(worker, &job, &ticket[4]) == CORE_CLOSED && ticket[4] == 0);
    CHECK(worker_join(worker, 0) == CORE_TIMEOUT);
    CHECK(worker->thread != NULL && worker->cancel != NULL);
    CHECK(SetEvent(context.release));
    test_heap_watch(false);
    CHECK(worker_join(worker, 5000) == CORE_OK);
    test_heap_watch(true);
    assert_closed(worker);
    bool seen[5] = {0};
    for (size_t i = 0; i < 4; ++i) {
        Event event = take(queue);
        CHECK(event.id >= 1 && event.id <= 4 && !seen[event.id]);
        seen[event.id] = true;
        CHECK(event.payload[0] == WORKER_CANCELLED);
    }
    CHECK(atomic_load(&context.runs) == 1);
    CHECK(!worker_cancel(worker, ticket[0]));
    CHECK(CloseHandle(context.started) && CloseHandle(context.release));
    finish_queue(queue);
    /* Queues/jobs/metadata share the budget; preserve space for future logs. */
    CHECK(arena_alloc(&core, 32 * MEMORY_KIB, 8) != NULL);
    CHECK(core.used <= MEMORY_BUDGET_CORE);
    arena_dispose(&core);
}

static void test_active_cancel(void)
{
    _Alignas(64) uint8_t memory[16384];
    Arena core;
    Worker *worker;
    EventQueue *queue;
    uint64_t ticket, next;
    JobContext context = {0};
    CHECK(arena_init(&core, memory, sizeof(memory)));
    CHECK(event_queue_create(&core, 8, &queue) == CORE_OK);
    context.completed = queue;
    context.started = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(context.started != NULL);
    test_heap_watch(false);
    CHECK(worker_start(&core, 1, (CoreClock){0}, &worker) == CORE_OK);
    test_heap_watch(true);
    WorkerJob job = job_description(&context, 10);
    job.run = run_until_cancelled;
    CHECK(worker_submit(worker, &job, &ticket) == CORE_OK);
    CHECK(WaitForSingleObject(context.started, 5000) == WAIT_OBJECT_0);
    CHECK(worker_cancel(worker, ticket));
    Event event = take(queue);
    CHECK(event.id == 10 && event.payload[0] == WORKER_CANCELLED);
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    job = job_description(&context, 11);
    CHECK(worker_submit(worker, &job, &next) == CORE_OK && next != ticket);
    CHECK(!worker_cancel(worker, ticket)); /* Reused slot, stale ticket. */
    event = take(queue);
    CHECK(event.id == 11 && event.payload[0] == WORKER_DONE);
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    job = job_description(&context, 12);
    job.not_before = core_deadline_after(core_clock_now(NULL), 20);
    CHECK(worker_submit(worker, &job, &next) == CORE_OK);
    event = take(queue); /* Real timer expiry, without an external wake. */
    CHECK(event.id == 12 && event.payload[0] == WORKER_DONE);
    CHECK(core_clock_now(NULL) >= job.not_before);
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    CHECK(worker_stop(worker) == CORE_OK);
    test_heap_watch(false);
    CHECK(worker_join(worker, 5000) == CORE_OK);
    test_heap_watch(true);
    assert_closed(worker);
    CHECK(atomic_load(&context.runs) == 3);
    CHECK(CloseHandle(context.started));
    finish_queue(queue);
    arena_dispose(&core);
}

static void test_scheduling(void)
{
    _Alignas(64) uint8_t memory[16384];
    _Atomic uint64_t now = 1000;
    CoreClock clock = {fake_now, &now};
    Arena core;
    Worker *worker;
    EventQueue *queue;
    uint64_t ticket, cancelled;
    JobContext context = {0};
    CHECK(arena_init(&core, memory, sizeof(memory)));
    CHECK(event_queue_create(&core, 8, &queue) == CORE_OK);
    context.completed = queue;
    test_heap_watch(false);
    CHECK(worker_start(&core, 4, clock, &worker) == CORE_OK);
    test_heap_watch(true);
    WorkerJob job = job_description(&context, 20);
    job.not_before = core_deadline_after(1000, core_backoff_ms(250, 1000, 2));
    CHECK(worker_submit(worker, &job, &ticket) == CORE_OK);
    job.request.id = 21;
    job.priority = WORKER_DISK;
    CHECK(worker_submit(worker, &job, &ticket) == CORE_OK);
    job.request.id = 22;
    job.not_before = 3000;
    job.deadline = 1500;
    CHECK(worker_submit(worker, &job, &ticket) == CORE_OK);
    job.request.id = 23;
    job.not_before = CORE_CLOCK_NEVER;
    job.deadline = CORE_CLOCK_NEVER;
    CHECK(worker_submit(worker, &job, &cancelled) == CORE_OK);
    CHECK(WaitForSingleObject(event_wait_handle(queue), 0) == WAIT_TIMEOUT);
    CHECK(worker_cancel(worker, cancelled));
    Event event = take(queue);
    CHECK(event.id == 23 && event.payload[0] == WORKER_CANCELLED);
    atomic_store(&now, 1500);
    CHECK(worker_notify_clock(worker) == CORE_OK);
    event = take(queue);
    CHECK(event.id == 22 && event.payload[0] == WORKER_EXPIRED);
    CHECK(atomic_load(&context.runs) == 0);
    job = job_description(&context, 24);
    job.not_before = 2000;
    CHECK(worker_submit(worker, &job, &ticket) == CORE_OK);
    atomic_store(&now, 2000);
    CHECK(worker_notify_clock(worker) == CORE_OK);
    event = take(queue);
    CHECK(event.id == 21 && event.payload[0] == WORKER_DONE); /* Disk first. */
    event = take(queue);
    CHECK(event.id == 20 && event.payload[0] == WORKER_DONE);
    event = take(queue);
    CHECK(event.id == 24 && event.payload[0] == WORKER_DONE); /* Art FIFO. */
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    CHECK(atomic_load(&context.runs) == 3);
    /* Quiescent fixture seeds the ticket counter; never wrap to a stale id. */
    AcquireSRWLockExclusive(&worker->lock);
    worker->stats.accepted = worker->stats.completed = UINT64_MAX;
    ReleaseSRWLockExclusive(&worker->lock);
    CHECK(worker_submit(worker, &job, &ticket) == CORE_EXHAUSTED && ticket == 0);
    CHECK(worker_stop(worker) == CORE_OK);
    test_heap_watch(false);
    CHECK(worker_join(worker, 5000) == CORE_OK);
    test_heap_watch(true);
    assert_closed(worker);
    finish_queue(queue);
    arena_dispose(&core);
}

static void test_invalid_and_budget(void)
{
    _Alignas(64) uint8_t memory[128];
    Arena core;
    Worker *worker = NULL;
    WorkerJob job = {0};
    uint64_t ticket;
    CHECK(arena_init(&core, memory, sizeof(memory)));
    CHECK(worker_start(NULL, 1, (CoreClock){0}, &worker) == CORE_INVALID);
    CHECK(worker_start(&core, 1, (CoreClock){0}, NULL) == CORE_INVALID);
    CHECK(worker_start(&core, 0, (CoreClock){0}, &worker) == CORE_INVALID);
    CHECK(worker_start(&core, WORKER_MAX_JOBS + 1, (CoreClock){0}, &worker) == CORE_INVALID);
    CHECK(worker_start(&core, 128, (CoreClock){0}, &worker) == CORE_NO_MEMORY);
    CHECK(worker == NULL && core.used == 0);
    CHECK(worker_submit(NULL, &job, &ticket) == CORE_INVALID && ticket == 0);
    CHECK(worker_submit(NULL, &job, NULL) == CORE_INVALID);
    CHECK(!worker_cancel(NULL, 1));
    CHECK(worker_stop(NULL) == CORE_INVALID && worker_join(NULL, 0) == CORE_INVALID);
    CHECK(worker_notify_clock(NULL) == CORE_INVALID && worker_idle_handle(NULL) == NULL);
    CHECK(worker_cancelled(NULL) && worker_cancel_wait(NULL) == 0);
    CHECK(worker_cancel_handle(NULL) == NULL);
    arena_dispose(&core);
}

typedef struct SubmitRace {
    Worker *worker;
    HANDLE start, threshold;
    _Atomic uint64_t completed;
} SubmitRace;

typedef struct Submitter {
    SubmitRace *race;
    uint64_t accepted;
} Submitter;

static void race_run(const Event *request, const CancelToken *token, void *context)
{
    (void)request; (void)token; (void)context;
}

static void race_complete(const Event *request, WorkerOutcome outcome, void *context)
{
    SubmitRace *race = context;
    (void)request;
    CHECK(outcome == WORKER_DONE || outcome == WORKER_CANCELLED);
    if (atomic_fetch_add(&race->completed, 1) + 1 == 1000) {
        CHECK(SetEvent(race->threshold));
    }
}

static DWORD WINAPI submit_until_stopped(void *context)
{
    Submitter *submitter = context;
    SubmitRace *race = submitter->race;
    WorkerJob job = {.run = race_run, .complete = race_complete, .context = race,
        .deadline = CORE_CLOCK_NEVER, .priority = WORKER_DISK};
    CHECK(WaitForSingleObject(race->start, 5000) == WAIT_OBJECT_0);
    test_heap_watch(true);
    for (;;) {
        uint64_t ticket;
        CoreResult result = worker_submit(race->worker, &job, &ticket);
        if (result == CORE_OK) { ++submitter->accepted; }
        else if (result == CORE_FULL) { SwitchToThread(); }
        else { CHECK(result == CORE_CLOSED); break; }
    }
    test_heap_watch(false);
    return 0;
}

static void test_submit_stop_race(void)
{
    _Alignas(64) uint8_t memory[16384];
    Arena core;
    Worker *worker;
    HANDLE threads[4];
    SubmitRace race = {0};
    Submitter submitters[4] = {0};
    uint64_t accepted = 0;
    CHECK(arena_init(&core, memory, sizeof(memory)));
    test_heap_watch(false);
    CHECK(worker_start(&core, 8, (CoreClock){0}, &worker) == CORE_OK);
    race.worker = worker;
    race.start = CreateEventW(NULL, TRUE, FALSE, NULL);
    race.threshold = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(race.start != NULL && race.threshold != NULL);
    for (size_t i = 0; i < 4; ++i) {
        submitters[i].race = &race;
        threads[i] = CreateThread(NULL, 0, submit_until_stopped, &submitters[i], 0, NULL);
        CHECK(threads[i] != NULL);
    }
    CHECK(SetEvent(race.start));
    CHECK(WaitForSingleObject(race.threshold, 5000) == WAIT_OBJECT_0);
    test_heap_watch(true);
    CHECK(worker_stop(worker) == CORE_OK);
    CHECK(WaitForMultipleObjects(4, threads, TRUE, 5000) == WAIT_OBJECT_0);
    for (size_t i = 0; i < 4; ++i) {
        accepted += submitters[i].accepted;
        CHECK(CloseHandle(threads[i]));
    }
    test_heap_watch(false);
    CHECK(worker_join(worker, 5000) == CORE_OK);
    test_heap_watch(true);
    assert_closed(worker);
    CHECK(accepted >= 1000 && atomic_load(&race.completed) == accepted);
    CHECK(worker_stats(worker).accepted == accepted);
    CHECK(CloseHandle(race.start) && CloseHandle(race.threshold));
    arena_dispose(&core);
}

void test_workers(void)
{
    test_invalid_and_budget();
    test_shutdown();
    test_active_cancel();
    test_scheduling();
    test_submit_stop_race();
}
