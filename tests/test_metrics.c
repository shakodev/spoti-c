#include "core/metrics.h"
#include "core/worker.h"
#include "support/fixture_clock.h"
#include "support/test_check.h"

static void test_arenas(void)
{
    Metrics metrics;
    MetricsArena first_binding = {0}, second_binding = {0}, vm_binding = {0};
    _Alignas(64) uint8_t first_memory[1024], second_memory[1024];
    Arena first, second;
    ArenaVm vm = {0};
    CHECK(metrics_init(&metrics) && metrics_balanced(&metrics));
    CHECK(arena_init(&first, first_memory, sizeof(first_memory)));
    CHECK(arena_init(&second, second_memory, sizeof(second_memory)));
    CHECK(metrics_attach_arena(&metrics, &first, &first_binding));
    CHECK(metrics_attach_arena(&metrics, &second, &second_binding));
    CHECK(!metrics_attach_arena(&metrics, &first, &first_binding));
    CHECK(arena_alloc(&first, 32, 8) != NULL && arena_alloc(&second, 64, 8) != NULL);
    MetricsSnapshot snapshot = metrics_snapshot(&metrics);
    CHECK(snapshot.arena_count == 2 && snapshot.arena_used == 96);
    CHECK(snapshot.arena_capacity == 2048 && snapshot.allocation_high_water == 96);
    CHECK(arena_alloc(&first, 256, 8) != NULL);
    CHECK(arena_reset(&first));
    snapshot = metrics_snapshot(&metrics);
    CHECK(snapshot.arena_used == 64 && snapshot.allocation_high_water == 352);
    CHECK(arena_alloc(&first, SIZE_MAX, 8) == NULL);
    CHECK(metrics_snapshot(&metrics).allocation_failures == 1);
    arena_dispose(&first); arena_dispose(&second);
    CHECK(metrics_balanced(&metrics));
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    CHECK(metrics_arena_prepare(&metrics, &vm_binding));
    CHECK(arena_vm_reserve(&vm, 2 * (size_t)info.dwPageSize, metrics_arena_hook, &vm_binding));
    CHECK(arena_vm_commit(&vm, 1));
    CHECK(arena_alloc(&vm.arena, 128, 8) != NULL);
    snapshot = metrics_snapshot(&metrics);
    CHECK(snapshot.reserved_bytes == 2 * (size_t)info.dwPageSize);
    CHECK(snapshot.committed_vm_bytes == info.dwPageSize && snapshot.arena_used == 128);
    CHECK(!metrics_balanced(&metrics));
    CHECK(arena_vm_release(&vm) && metrics_balanced(&metrics));
    CHECK(!first_binding.active && !second_binding.active && !vm_binding.active);
}

static void test_owned_resources(void)
{
    Metrics metrics;
    MetricsAllocation allocation = {0};
    MetricsHandle handle = {0};
    CHECK(metrics_init(&metrics));
    void *memory = HeapAlloc(GetProcessHeap(), 0, 64);
    CHECK(memory != NULL && metrics_allocation_acquire(&metrics, &allocation, 64));
    CHECK(!metrics_allocation_acquire(&metrics, &allocation, 64));
    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(event != NULL && metrics_handle_acquire(&metrics, &handle, (uintptr_t)event));
    MetricsSnapshot snapshot = metrics_snapshot(&metrics);
    CHECK(snapshot.owned_allocations == 1 && snapshot.owned_bytes == 64);
    CHECK(snapshot.owned_high_water == 64 && snapshot.handles == 1 && snapshot.handle_high_water == 1);
    CHECK(!metrics_balanced(&metrics));
    CHECK(CloseHandle(event) && metrics_handle_release(&handle));
    CHECK(HeapFree(GetProcessHeap(), 0, memory) && metrics_allocation_release(&allocation));
    CHECK(!metrics_handle_release(&handle) && !metrics_allocation_release(&allocation));
    CHECK(metrics_balanced(&metrics));
}

static DWORD WINAPI count_audio(void *context)
{
    Metrics *metrics = context;
    for (size_t i = 0; i < 250000; ++i) { metrics_count(metrics, METRIC_UNDERRUN, 1); }
    return 0;
}

static void test_atomic_counts(void)
{
    Metrics metrics;
    HANDLE threads[4];
    CHECK(metrics_init(&metrics));
    /* Audio counters must finish even while a snapshot/ownership lock is held. */
    AcquireSRWLockExclusive(&metrics.lock);
    for (size_t i = 0; i < 4; ++i) {
        threads[i] = CreateThread(NULL, 0, count_audio, &metrics, 0, NULL);
        CHECK(threads[i] != NULL);
    }
    CHECK(WaitForMultipleObjects(4, threads, TRUE, 5000) == WAIT_OBJECT_0);
    metrics_count(&metrics, METRIC_WAKE, 7);
    metrics_count(&metrics, METRIC_RENDER, 3);
    ReleaseSRWLockExclusive(&metrics.lock);
    for (size_t i = 0; i < 4; ++i) { CHECK(CloseHandle(threads[i])); }
    MetricsSnapshot snapshot = metrics_snapshot(&metrics);
    CHECK(snapshot.underruns == 1000000 && snapshot.wake_count == 7 && snapshot.render_count == 3);
    CHECK(metrics_balanced(&metrics));
    metrics_count(&metrics, METRIC_RENDER, UINT64_MAX);
    CHECK(metrics_snapshot(&metrics).counter_overflow && !metrics_balanced(&metrics));
}

static void no_work(const Event *event, const CancelToken *token, void *context)
{
    (void)event; (void)token; (void)context;
}
static void no_completion(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; (void)outcome; (void)context;
}

static void test_integration(void)
{
    _Alignas(64) uint8_t memory[16384];
    Arena core;
    Metrics metrics;
    MetricsArena binding = {0};
    EventQueue *queue;
    Worker *worker;
    FixtureClock clock;
    fixture_clock_init(&clock, 1234);
    CoreClock source = fixture_clock_source(&clock);
    CHECK(core_clock_now(&source) == 1234 && fixture_clock_advance(&clock, 66));
    CHECK(core_clock_now(&source) == 1300 && !fixture_clock_advance(&clock, UINT64_MAX));
    CHECK(metrics_init(&metrics) && arena_init(&core, memory, sizeof(memory)));
    CHECK(metrics_attach_arena(&metrics, &core, &binding));
    CHECK(event_queue_create(&core, 4, &queue) == CORE_OK);
    CHECK(worker_start(&core, 2, source, &worker) == CORE_OK);
    CHECK(metrics_snapshot(&metrics).handles == 5);
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    WorkerJob job = {.run = no_work, .complete = no_completion, .deadline = CORE_CLOCK_NEVER};
    uint64_t ticket;
    CHECK(worker_submit(worker, &job, &ticket) == CORE_OK);
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    CHECK(worker_stop(worker) == CORE_OK && worker_join(worker, 5000) == CORE_OK);
    CHECK(metrics_snapshot(&metrics).handles == 1 && metrics_snapshot(&metrics).wake_count >= 1);
    Event event = {.id = 123};
    size_t count;
    CHECK(event_post(queue, &event) == CORE_OK);
    CHECK(event_drain(queue, &event, 1, &count) == CORE_OK && count == 1);
    CHECK(event_queue_close(queue) == CORE_OK && event_queue_destroy(queue) == CORE_OK);
    CHECK(metrics_snapshot(&metrics).handles == 0);
    CHECK(metrics_snapshot(&metrics).arena_used == core.used);
    arena_dispose(&core);
    CHECK(metrics_balanced(&metrics));
}

int main(void)
{
    DWORD before, after;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    CHECK(!metrics_init(NULL) && !metrics_balanced(NULL));
    test_arenas(); test_owned_resources(); test_atomic_counts(); test_integration();
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after) && before == after);
    puts("Metrics arena/VM peaks, ownership leases, audio counters and shutdown balance passed");
    return 0;
}
