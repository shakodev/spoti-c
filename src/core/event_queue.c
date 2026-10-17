#include "event_queue.h"

CoreResult event_queue_create(Arena *core, size_t capacity, EventQueue **output)
{
    ArenaMark mark;
    EventQueue *queue;
    HANDLE wake;
    if (output == NULL) { return CORE_INVALID; }
    *output = NULL;
    if (core == NULL || core->capacity > MEMORY_BUDGET_CORE || capacity == 0 ||
        capacity > (size_t)PTRDIFF_MAX / sizeof(Event)) { return CORE_INVALID; }
    wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (wake == NULL) { return CORE_OS_ERROR; }
    mark = arena_mark(core);
    queue = arena_alloc(core, sizeof(*queue), _Alignof(EventQueue));
    if (queue != NULL) {
        *queue = (EventQueue){0};
        queue->slots = arena_alloc(core, capacity * sizeof(Event), _Alignof(Event));
    }
    if (queue == NULL || queue->slots == NULL) {
        CloseHandle(wake);
        (void)arena_rewind(core, mark);
        return CORE_NO_MEMORY;
    }
    InitializeSRWLock(&queue->lock);
    queue->wake = wake;
    queue->capacity = capacity;
    *output = queue;
    return CORE_OK;
}

CoreResult event_post(EventQueue *queue, const Event *event)
{
    CoreResult result = CORE_OK;
    if (queue == NULL || event == NULL) { return CORE_INVALID; }
    AcquireSRWLockExclusive(&queue->lock);
    if (queue->closed) { result = CORE_CLOSED; }
    else if (queue->count == queue->capacity) { result = CORE_FULL; }
    /* Signal before committing, under the lock. A woken owner cannot drain
     * until the payload is committed; a signal failure accepts nothing. */
    else if (!SetEvent(queue->wake)) { result = CORE_OS_ERROR; }
    else {
        queue->slots[(queue->head + queue->count) % queue->capacity] = *event;
        ++queue->count;
    }
    ReleaseSRWLockExclusive(&queue->lock);
    return result;
}

CoreResult event_drain(EventQueue *queue, Event *output, size_t limit, size_t *count)
{
    CoreResult result = CORE_OK;
    if (count == NULL) { return CORE_INVALID; }
    *count = 0;
    if (queue == NULL || (output == NULL && limit != 0)) { return CORE_INVALID; }
    AcquireSRWLockExclusive(&queue->lock);
    while (*count < limit && queue->count != 0) {
        output[*count] = queue->slots[queue->head];
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        ++*count;
    }
    if (queue->count == 0) {
        if (queue->closed) { result = CORE_CLOSED; }
        else if (!ResetEvent(queue->wake)) { result = CORE_OS_ERROR; }
    }
    ReleaseSRWLockExclusive(&queue->lock);
    return result;
}

HANDLE event_wait_handle(const EventQueue *queue)
{
    return queue == NULL ? NULL : queue->wake;
}

CoreResult event_queue_close(EventQueue *queue)
{
    CoreResult result = CORE_OK;
    if (queue == NULL) { return CORE_INVALID; }
    AcquireSRWLockExclusive(&queue->lock);
    if (!SetEvent(queue->wake)) { result = CORE_OS_ERROR; }
    else { queue->closed = true; }
    ReleaseSRWLockExclusive(&queue->lock);
    return result;
}

CoreResult event_queue_destroy(EventQueue *queue)
{
    if (queue == NULL || queue->wake == NULL) { return CORE_INVALID; }
    if (!queue->closed || queue->count != 0) { return CORE_INVALID; }
    if (!CloseHandle(queue->wake)) { return CORE_OS_ERROR; }
    queue->wake = NULL;
    return CORE_OK;
}
