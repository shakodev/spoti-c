#include "worker.h"

enum { SLOT_FREE, SLOT_QUEUED, SLOT_RUNNING, SLOT_COMPLETING };

bool worker_cancelled(const CancelToken *token)
{
    return token == NULL || atomic_load_explicit(&token->requested, memory_order_acquire) ||
        core_deadline_expired(core_clock_now(&token->clock), token->deadline);
}

uint32_t worker_cancel_wait(const CancelToken *token)
{
    return worker_cancelled(token) ? 0 :
        core_deadline_wait(core_clock_now(&token->clock), token->deadline);
}

HANDLE worker_cancel_handle(const CancelToken *token)
{
    return token == NULL ? NULL : token->wake;
}

static bool close_owned(HANDLE *handle)
{
    if (*handle != NULL) {
        if (!CloseHandle(*handle)) { return false; }
        *handle = NULL;
    }
    return true;
}

/* Called with the owner lock held. Cancelled/expired jobs become ready even
 * before not_before, so cancellation never waits for a backoff deadline. */
static size_t select_job(Worker *worker, uint64_t now, uint64_t *next)
{
    size_t selected = worker->capacity;
    *next = CORE_CLOCK_NEVER;
    for (size_t i = 0; i < worker->capacity; ++i) {
        WorkerSlot *slot = &worker->slots[i];
        if (slot->state != SLOT_QUEUED) { continue; }
        if (atomic_load_explicit(&slot->cancel.requested, memory_order_acquire) ||
            core_deadline_expired(now, slot->job.deadline) || slot->job.not_before <= now) {
            if (selected == worker->capacity ||
                slot->job.priority < worker->slots[selected].job.priority ||
                (slot->job.priority == worker->slots[selected].job.priority &&
                 slot->ticket < worker->slots[selected].ticket)) { selected = i; }
        } else {
            if (slot->job.not_before < *next) { *next = slot->job.not_before; }
            if (slot->job.deadline < *next) { *next = slot->job.deadline; }
        }
    }
    return selected;
}

static void execute(Worker *worker, WorkerSlot *slot)
{
    WorkerOutcome outcome;
    if (!worker_cancelled(&slot->cancel)) {
        slot->job.run(&slot->job.request, &slot->cancel, slot->job.context);
    }
    AcquireSRWLockExclusive(&worker->lock);
    if (atomic_load_explicit(&slot->cancel.requested, memory_order_acquire)) {
        outcome = WORKER_CANCELLED;
    } else if (core_deadline_expired(core_clock_now(&worker->clock), slot->job.deadline)) {
        outcome = WORKER_EXPIRED;
    } else { outcome = WORKER_DONE; }
    /* Outcome is now committed; cancellation can no longer claim this ticket. */
    slot->state = SLOT_COMPLETING;
    ReleaseSRWLockExclusive(&worker->lock);
    slot->job.complete(&slot->job.request, outcome, slot->job.context);
    AcquireSRWLockExclusive(&worker->lock);
    slot->state = SLOT_FREE;
    --worker->stats.pending;
    ++worker->stats.completed;
    if (worker->stats.pending == 0) { SetEvent(worker->idle); }
    ReleaseSRWLockExclusive(&worker->lock);
}

static DWORD WINAPI worker_main(void *context)
{
    Worker *worker = context;
    for (;;) {
        uint64_t next;
        uint64_t now;
        size_t selected;
        DWORD timeout;
        AcquireSRWLockExclusive(&worker->lock);
        now = core_clock_now(&worker->clock);
        selected = select_job(worker, now, &next);
        if (selected != worker->capacity) {
            WorkerSlot *slot = &worker->slots[selected];
            slot->state = SLOT_RUNNING;
            ResetEvent(worker->cancel);
            ReleaseSRWLockExclusive(&worker->lock);
            execute(worker, slot);
            continue;
        }
        if (worker->stats.stopping && worker->stats.pending == 0) {
            ReleaseSRWLockExclusive(&worker->lock);
            return 0;
        }
        /* The empty/ready check and ResetEvent share the submission lock.
         * Publish an idle snapshot before blocking, never poll when empty. */
        ResetEvent(worker->wake);
        if (worker->stats.waits != UINT64_MAX) { ++worker->stats.waits; }
        if (worker->stats.pending == 0) { SetEvent(worker->idle); }
        timeout = core_deadline_wait(now, next);
        ReleaseSRWLockExclusive(&worker->lock);
        if (WaitForSingleObject(worker->wake, timeout) == WAIT_FAILED) {
            DWORD error = GetLastError();
            (void)worker_stop(worker);
            AcquireSRWLockExclusive(&worker->lock);
            worker->stats.error = error;
            ReleaseSRWLockExclusive(&worker->lock);
        }
    }
}

CoreResult worker_start(Arena *core, size_t capacity, CoreClock clock, Worker **output)
{
    ArenaMark mark;
    Worker *worker;
    if (output == NULL) { return CORE_INVALID; }
    *output = NULL;
    if (core == NULL || core->capacity > MEMORY_BUDGET_CORE || capacity == 0 ||
        capacity > WORKER_MAX_JOBS) { return CORE_INVALID; }
    mark = arena_mark(core);
    worker = arena_alloc(core, sizeof(*worker), _Alignof(Worker));
    if (worker == NULL) { return CORE_NO_MEMORY; }
    *worker = (Worker){0};
    worker->slots = arena_alloc(core, capacity * sizeof(WorkerSlot), _Alignof(WorkerSlot));
    if (worker->slots == NULL) {
        (void)arena_rewind(core, mark);
        return CORE_NO_MEMORY;
    }
    InitializeSRWLock(&worker->lock);
    worker->capacity = capacity;
    worker->clock = clock;
    for (size_t i = 0; i < capacity; ++i) {
        worker->slots[i].state = SLOT_FREE;
        atomic_init(&worker->slots[i].cancel.requested, false);
    }
    worker->wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    worker->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    worker->idle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (worker->wake != NULL && worker->cancel != NULL && worker->idle != NULL) {
        /* Publish the thread handle before the new owner can enter its loop. */
        AcquireSRWLockExclusive(&worker->lock);
        worker->thread = CreateThread(NULL, 0, worker_main, worker, 0, NULL);
        ReleaseSRWLockExclusive(&worker->lock);
    }
    if (worker->thread == NULL) {
        (void)close_owned(&worker->wake);
        (void)close_owned(&worker->cancel);
        (void)close_owned(&worker->idle);
        (void)arena_rewind(core, mark);
        return CORE_OS_ERROR;
    }
    *output = worker;
    return CORE_OK;
}

CoreResult worker_submit(Worker *worker, const WorkerJob *job, uint64_t *ticket)
{
    CoreResult result = CORE_FULL;
    if (ticket == NULL) { return CORE_INVALID; }
    *ticket = 0;
    if (worker == NULL || job == NULL || job->run == NULL || job->complete == NULL ||
        (job->priority != WORKER_DISK && job->priority != WORKER_ART)) { return CORE_INVALID; }
    AcquireSRWLockExclusive(&worker->lock);
    if (worker->stats.stopping) { result = CORE_CLOSED; }
    else if (worker->stats.accepted == UINT64_MAX) { result = CORE_EXHAUSTED; }
    else {
        for (size_t i = 0; i < worker->capacity; ++i) {
            WorkerSlot *slot = &worker->slots[i];
            if (slot->state != SLOT_FREE) { continue; }
            if (!SetEvent(worker->wake) || !ResetEvent(worker->idle)) { result = CORE_OS_ERROR; }
            else {
                slot->job = *job;
                slot->cancel.wake = worker->cancel;
                slot->cancel.clock = worker->clock;
                slot->cancel.deadline = job->deadline;
                atomic_store_explicit(&slot->cancel.requested, false, memory_order_release);
                slot->ticket = ++worker->stats.accepted;
                slot->state = SLOT_QUEUED;
                ++worker->stats.pending;
                *ticket = slot->ticket;
                result = CORE_OK;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&worker->lock);
    return result;
}

bool worker_cancel(Worker *worker, uint64_t ticket)
{
    bool found = false;
    if (worker == NULL || ticket == 0) { return false; }
    AcquireSRWLockExclusive(&worker->lock);
    for (size_t i = 0; i < worker->capacity; ++i) {
        WorkerSlot *slot = &worker->slots[i];
        if ((slot->state == SLOT_QUEUED || slot->state == SLOT_RUNNING) && slot->ticket == ticket) {
            atomic_store_explicit(&slot->cancel.requested, true, memory_order_release);
            found = SetEvent(worker->wake) != 0;
            if (slot->state == SLOT_RUNNING && !SetEvent(worker->cancel)) { found = false; }
            break;
        }
    }
    ReleaseSRWLockExclusive(&worker->lock);
    return found;
}

CoreResult worker_stop(Worker *worker)
{
    CoreResult result = CORE_OK;
    if (worker == NULL) { return CORE_INVALID; }
    AcquireSRWLockExclusive(&worker->lock);
    if (worker->thread == NULL) {
        ReleaseSRWLockExclusive(&worker->lock);
        return CORE_CLOSED;
    }
    worker->stats.stopping = true;
    for (size_t i = 0; i < worker->capacity; ++i) {
        if (worker->slots[i].state == SLOT_QUEUED || worker->slots[i].state == SLOT_RUNNING) {
            atomic_store_explicit(&worker->slots[i].cancel.requested, true, memory_order_release);
        }
    }
    if (!SetEvent(worker->cancel) || !SetEvent(worker->wake)) { result = CORE_OS_ERROR; }
    ReleaseSRWLockExclusive(&worker->lock);
    return result;
}

CoreResult worker_join(Worker *worker, DWORD timeout_ms)
{
    DWORD wait;
    if (worker == NULL) { return CORE_INVALID; }
    if (worker->thread == NULL) { return CORE_CLOSED; }
    if (GetThreadId(worker->thread) == GetCurrentThreadId() || !worker_stats(worker).stopping) {
        return CORE_INVALID;
    }
    wait = WaitForSingleObject(worker->thread, timeout_ms);
    if (wait == WAIT_TIMEOUT) { return CORE_TIMEOUT; }
    if (wait != WAIT_OBJECT_0) { return CORE_OS_ERROR; }
    if (!close_owned(&worker->wake) || !close_owned(&worker->cancel) ||
        !close_owned(&worker->idle) || !close_owned(&worker->thread)) { return CORE_OS_ERROR; }
    return worker->stats.error == ERROR_SUCCESS ? CORE_OK : CORE_OS_ERROR;
}

HANDLE worker_idle_handle(const Worker *worker)
{
    return worker == NULL ? NULL : worker->idle;
}

WorkerStats worker_stats(Worker *worker)
{
    WorkerStats stats = {0};
    if (worker != NULL) {
        AcquireSRWLockShared(&worker->lock);
        stats = worker->stats;
        ReleaseSRWLockShared(&worker->lock);
    }
    return stats;
}

CoreResult worker_notify_clock(Worker *worker)
{
    CoreResult result;
    if (worker == NULL) { return CORE_INVALID; }
    AcquireSRWLockExclusive(&worker->lock);
    result = worker->thread == NULL ? CORE_CLOSED :
        (SetEvent(worker->wake) ? CORE_OK : CORE_OS_ERROR);
    ReleaseSRWLockExclusive(&worker->lock);
    return result;
}
