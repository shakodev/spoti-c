#include "core_event_test.h"

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static _Atomic size_t heap_calls;
static _Thread_local bool watch_heap;
static int __cdecl count_heap(int type, void *data, size_t size, int block,
                              long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block;
    (void)request; (void)file; (void)line;
    if (watch_heap) { atomic_fetch_add(&heap_calls, 1); }
    return 1;
}
#endif

void test_heap_watch(bool enabled)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    watch_heap = enabled;
#else
    (void)enabled;
#endif
}

static uint64_t fake_now(void *context)
{
    return atomic_load((_Atomic uint64_t *)context);
}

static void test_clock(void)
{
    _Atomic uint64_t time = 100;
    CoreClock clock = {fake_now, &time};
    uint64_t deadline = core_deadline_after(core_clock_now(&clock), 25);
    CHECK(deadline == 125 && core_deadline_wait(100, deadline) == 25);
    CHECK(!core_deadline_expired(124, deadline));
    atomic_store(&time, 125);
    CHECK(core_deadline_expired(core_clock_now(&clock), deadline));
    CHECK(core_deadline_wait(125, deadline) == 0 && core_deadline_wait(126, deadline) == 0);
    CHECK(!core_deadline_expired(UINT64_MAX, CORE_CLOCK_NEVER));
    CHECK(core_deadline_wait(0, CORE_CLOCK_NEVER) == INFINITE);
    CHECK(core_deadline_after(UINT64_MAX - 5, 10) == UINT64_MAX - 1);
    CHECK(core_deadline_after(0, UINT64_MAX) == UINT64_MAX - 1);
    CHECK(core_deadline_wait(0, UINT64_MAX - 1) == INFINITE - 1);
    CHECK(core_deadline_after(0, 0) == 0 && core_deadline_expired(0, 0));
    CHECK(core_backoff_ms(10, 1000, 0) == 10);
    CHECK(core_backoff_ms(10, 1000, 1) == 20);
    CHECK(core_backoff_ms(10, 1000, 6) == 640);
    CHECK(core_backoff_ms(10, 1000, 7) == 1000);
    CHECK(core_backoff_ms(10, 1000, UINT32_MAX) == 1000);
    CHECK(core_backoff_ms(UINT64_MAX, UINT64_MAX, 63) == UINT64_MAX);
    CHECK(core_backoff_ms(1, UINT64_MAX, 63) == (UINT64_C(1) << 63));
    CHECK(core_backoff_ms(0, 100, 10) == 0 && core_backoff_ms(10, 0, 0) == 0);
    uint64_t system = core_clock_now(NULL);
    CHECK(core_clock_now(NULL) >= system);
}

static void test_queue_basics(void)
{
    _Alignas(64) uint8_t memory[8192];
    Arena core;
    EventQueue *queue = NULL;
    size_t count = 999;
    Event input = {.type = 7, .generation = 3, .id = 19, .payload = {11, 12, 13, 14}};
    Event output[4];
    CHECK(arena_init(&core, memory, sizeof(memory)));
    CHECK(event_queue_create(NULL, 3, &queue) == CORE_INVALID && queue == NULL);
    CHECK(event_queue_create(&core, 3, NULL) == CORE_INVALID);
    CHECK(event_queue_create(&core, 0, &queue) == CORE_INVALID);
    CHECK(event_queue_create(&core, SIZE_MAX, &queue) == CORE_INVALID);
    CHECK(event_queue_create(&core, 1000, &queue) == CORE_NO_MEMORY);
    CHECK(core.used == 0 && queue == NULL);
    CHECK(event_queue_create(&core, 3, &queue) == CORE_OK);
    CHECK(event_wait_handle(NULL) == NULL);
    CHECK(WaitForSingleObject(event_wait_handle(queue), 0) == WAIT_TIMEOUT);
    CHECK(event_post(NULL, &input) == CORE_INVALID && event_post(queue, NULL) == CORE_INVALID);
    CHECK(event_drain(queue, output, 4, NULL) == CORE_INVALID);
    CHECK(event_drain(NULL, output, 4, &count) == CORE_INVALID && count == 0);
    CHECK(event_drain(queue, NULL, 4, &count) == CORE_INVALID && count == 0);
    CHECK(event_queue_destroy(queue) == CORE_INVALID);
    for (size_t i = 0; i < 3; ++i) {
        input.id = i;
        CHECK(event_post(queue, &input) == CORE_OK);
    }
    CHECK(event_post(queue, &input) == CORE_FULL);
    HANDLE wake = event_wait_handle(queue);
    CHECK(MsgWaitForMultipleObjectsEx(1, &wake, 0, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0);
    CHECK(event_drain(queue, output, 2, &count) == CORE_OK && count == 2);
    CHECK(output[0].id == 0 && output[1].id == 1 && output[1].generation == 3);
    CHECK(output[1].payload[3] == 14);
    CHECK(WaitForSingleObject(wake, 0) == WAIT_OBJECT_0);
    input.id = 3;
    CHECK(event_post(queue, &input) == CORE_OK); /* Wrap slot index. */
    CHECK(event_drain(queue, output, 4, &count) == CORE_OK && count == 2);
    CHECK(output[0].id == 2 && output[1].id == 3);
    CHECK(WaitForSingleObject(wake, 0) == WAIT_TIMEOUT);
    CHECK(event_drain(queue, NULL, 0, &count) == CORE_OK && count == 0);
    CHECK(event_post(queue, &input) == CORE_OK);
    CHECK(event_queue_close(queue) == CORE_OK);
    CHECK(event_queue_destroy(queue) == CORE_INVALID); /* Accepted record retained. */
    CHECK(event_post(queue, &input) == CORE_CLOSED);
    CHECK(event_drain(queue, output, 4, &count) == CORE_CLOSED && count == 1);
    CHECK(WaitForSingleObject(wake, 0) == WAIT_OBJECT_0);
    CHECK(event_drain(queue, output, 4, &count) == CORE_CLOSED && count == 0);
    CHECK(event_queue_close(queue) == CORE_OK && event_queue_destroy(queue) == CORE_OK);
    CHECK(queue->wake == NULL && event_queue_destroy(queue) == CORE_INVALID);
    arena_dispose(&core);
    Arena too_large = {.capacity = MEMORY_BUDGET_CORE + 1};
    CHECK(event_queue_create(&too_large, 1, &queue) == CORE_INVALID);
}

typedef struct Producer {
    EventQueue *queue;
    HANDLE start;
    uint32_t index;
    uint64_t accepted, full;
    bool saw_close;
} Producer;

static DWORD WINAPI post_many(void *context)
{
    Producer *producer = context;
    CHECK(WaitForSingleObject(producer->start, 5000) == WAIT_OBJECT_0);
    test_heap_watch(true);
    while (producer->accepted < 20000) {
        Event event = {.type = producer->index, .generation = producer->index + 1,
            .id = producer->accepted, .payload = {~producer->accepted, 0, 0, 12345}};
        CoreResult result = event_post(producer->queue, &event);
        if (result == CORE_OK) { ++producer->accepted; }
        else if (result == CORE_FULL) { ++producer->full; SwitchToThread(); }
        else { CHECK(result == CORE_CLOSED); producer->saw_close = true; break; }
    }
    test_heap_watch(false);
    return 0;
}

static void test_post_reset_race(bool close_while_posting)
{
    _Alignas(64) uint8_t memory[4096];
    Arena core;
    EventQueue *queue;
    HANDLE threads[4];
    Producer producers[4] = {0};
    uint64_t received[4] = {0}, total = 0;
    CHECK(arena_init(&core, memory, sizeof(memory)));
    CHECK(event_queue_create(&core, 1, &queue) == CORE_OK);
    HANDLE start = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(start != NULL);
    test_heap_watch(false);
    for (uint32_t i = 0; i < 4; ++i) {
        producers[i].queue = queue;
        producers[i].start = start;
        producers[i].index = i;
        threads[i] = CreateThread(NULL, 0, post_many, &producers[i], 0, NULL);
        CHECK(threads[i] != NULL);
    }
    CHECK(SetEvent(start));
    test_heap_watch(true);
    while (total < 80000) {
        Event event;
        size_t count;
        CHECK(WaitForSingleObject(event_wait_handle(queue), 5000) == WAIT_OBJECT_0);
        CoreResult result = event_drain(queue, &event, 1, &count);
        CHECK(result == CORE_OK || result == CORE_CLOSED);
        if (count != 0) {
            CHECK(event.type < 4 && event.generation == event.type + 1);
            CHECK(event.id == received[event.type] && event.payload[0] == ~event.id);
            CHECK(event.payload[3] == 12345);
            ++received[event.type];
            ++total;
        }
        if (close_while_posting && total == 1000) { CHECK(event_queue_close(queue) == CORE_OK); }
        if (result == CORE_CLOSED) { break; }
    }
    test_heap_watch(false);
    CHECK(WaitForMultipleObjects(4, threads, TRUE, 5000) == WAIT_OBJECT_0);
    for (size_t i = 0; i < 4; ++i) {
        DWORD result;
        CHECK(GetExitCodeThread(threads[i], &result) && result == 0);
        CHECK(CloseHandle(threads[i]));
        CHECK(producers[i].accepted == received[i]);
        CHECK(producers[i].saw_close == close_while_posting);
    }
    CHECK(CloseHandle(start));
    test_heap_watch(true);
    CHECK(close_while_posting || total == 80000);
    CHECK(event_queue_close(queue) == CORE_OK);
    CHECK(event_queue_destroy(queue) == CORE_OK);
    arena_dispose(&core);
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(count_heap);
#endif
    DWORD handles_before, handles_after;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    test_heap_watch(true);
    test_clock();
    test_queue_basics();
    test_post_reset_race(false);
    test_post_reset_race(true);
    test_workers();
    test_heap_watch(false);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    CHECK(handles_after == handles_before);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(previous);
    CHECK(atomic_load(&heap_calls) == 0);
#endif
    puts("MPSC reset races, worker cancellation, clock deadlines and handle balance passed");
    return 0;
}
