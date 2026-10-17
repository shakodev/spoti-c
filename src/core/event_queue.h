#ifndef SPOTIC_CORE_EVENT_QUEUE_H
#define SPOTIC_CORE_EVENT_QUEUE_H

#include "arena.h"
#include "memory_budget.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef enum CoreResult {
    CORE_OK, CORE_FULL, CORE_CLOSED, CORE_INVALID, CORE_NO_MEMORY,
    CORE_OS_ERROR, CORE_TIMEOUT, CORE_EXHAUSTED
} CoreResult;

typedef struct Event {
    uint32_t type, generation;
    uint64_t id;
    uint64_t payload[4]; /* Fixed values/handles, never implicitly owned pointers. */
} Event;

typedef struct EventQueue {
    SRWLOCK lock;
    HANDLE wake;
    Event *slots;
    size_t capacity, head, count;
    bool closed;
} EventQueue;

/* Setup owner allocates queues, workers and future logs from ONE shared core
 * arena of at most MEMORY_BUDGET_CORE (128 KiB), including metadata. Never reset
 * it until every producer/consumer is quiescent and every object is destroyed.
 * Creation returns *output=NULL on failure and rewinds arena usage on failure.
 * Arena high-water retains failed setup peaks. No hidden heap/growth. */
CoreResult event_queue_create(Arena *core, size_t capacity, EventQueue **output);
/* MPSC posts and one owner drain. Short SRW critical sections: never call from
 * a WASAPI callback. FULL retains ownership with the caller; never overwrites. */
CoreResult event_post(EventQueue *queue, const Event *event);
/* count is always set. CLOSED means closed and empty AFTER this drain; count
 * may still contain the final batch. An empty open queue returns OK/count=0. */
CoreResult event_drain(EventQueue *queue, Event *output, size_t limit, size_t *count);
/* Borrowed manual-reset handle supports MsgWaitForMultipleObjectsEx. Do not
 * ResetEvent/CloseHandle yourself. Empty checks/reset are synchronized with post. */
HANDLE event_wait_handle(const EventQueue *queue);
CoreResult event_queue_close(EventQueue *queue);
/* Close admission first, stop/join producers, drain accepted events, then destroy
 * with ALL users quiescent. Destroy never reclaims arena storage individually. */
CoreResult event_queue_destroy(EventQueue *queue);

#endif
