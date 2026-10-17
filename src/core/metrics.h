#ifndef SPOTIC_CORE_METRICS_H
#define SPOTIC_CORE_METRICS_H

#include "arena.h"
#include <stdatomic.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

typedef enum MetricCounter {
    METRIC_WAKE, METRIC_RENDER, METRIC_UNDERRUN, METRIC_LOG_DROP,
    METRIC_LOG_WRITTEN, METRIC_LOG_WRITE_FAILED, METRIC_SIGNAL, METRIC_COUNTER_COUNT
} MetricCounter;

typedef struct MetricsSnapshot {
    uint64_t arena_count, arena_used, arena_capacity, allocation_high_water;
    uint64_t reserved_bytes, committed_vm_bytes;
    uint64_t owned_allocations, owned_bytes, owned_high_water;
    uint64_t handles, handle_high_water, allocation_failures, errors;
    uint64_t wake_count, render_count, underruns, log_dropped, log_written;
    uint64_t log_write_failed, signals;
    bool counter_overflow;
} MetricsSnapshot;

typedef struct Metrics {
    SRWLOCK lock;
    MetricsSnapshot ledger;
    _Atomic uint64_t counters[METRIC_COUNTER_COUNT];
    _Atomic bool overflow;
} Metrics;

typedef struct MetricsAllocation { Metrics *owner; uint64_t bytes; } MetricsAllocation;
typedef struct MetricsHandle { Metrics *owner; uintptr_t value; } MetricsHandle;
typedef struct MetricsArena {
    Metrics *owner;
    uint64_t used, capacity, reserved, committed;
    bool active;
} MetricsArena;

/* Initialize before publication; reject non-lock-free counters. Keep metrics
 * alive until all watched arenas, resource leases and updating threads end.
 * Memory/resource operations use a short SRW lock and are NOT audio-safe. */
bool metrics_init(Metrics *metrics);
bool metrics_allocation_acquire(Metrics *metrics, MetricsAllocation *lease, size_t bytes);
bool metrics_allocation_release(MetricsAllocation *lease);
bool metrics_handle_acquire(Metrics *metrics, MetricsHandle *lease, uintptr_t value);
/* Record release AFTER the actual OS close/free succeeds. Leases start at {0}
 * and cannot be copied while live. API tracks ownership, never performs the I/O. */
bool metrics_handle_release(MetricsHandle *lease);
/* Bind a zero-initialized binding BEFORE the arena's first allocation. Attaching
 * after unobserved use is rejected rather than inventing historical peaks. */
bool metrics_attach_arena(Metrics *metrics, Arena *arena, MetricsArena *binding);
/* For VM reserve: prepare binding, then pass metrics_arena_hook/&binding into
 * arena_vm_reserve. SNAPSHOT activates it; DISPOSE/RELEASE retire it. */
bool metrics_arena_prepare(Metrics *metrics, MetricsArena *binding);
void metrics_arena_hook(void *context, const Arena *arena, ArenaEvent event, size_t bytes);
Metrics *metrics_arena_owner(const Arena *arena);

/* Audio callback path: lock-free atomic updates ONLY. No lock, event, clock,
 * formatting, callback, allocation, or retry loop. Overflow is flagged, not hidden. */
void metrics_count(Metrics *metrics, MetricCounter counter, uint64_t amount);
MetricsSnapshot metrics_snapshot(Metrics *metrics);
/* At quiescent shutdown: all tracked resources/arenas released, no accounting
 * errors/overflow. This does not claim to inspect untracked OS/driver memory. */
bool metrics_balanced(Metrics *metrics);

#endif
