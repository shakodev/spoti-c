#include "metrics.h"

bool metrics_init(Metrics *metrics)
{
    if (metrics == NULL) { return false; }
    *metrics = (Metrics){0};
    InitializeSRWLock(&metrics->lock);
    atomic_init(&metrics->overflow, false);
    for (size_t i = 0; i < METRIC_COUNTER_COUNT; ++i) {
        atomic_init(&metrics->counters[i], 0);
        if (!atomic_is_lock_free(&metrics->counters[i])) { return false; }
    }
    return atomic_is_lock_free(&metrics->overflow);
}

static bool add(uint64_t *value, uint64_t amount)
{
    if (*value > UINT64_MAX - amount) { return false; }
    *value += amount;
    return true;
}

bool metrics_allocation_acquire(Metrics *metrics, MetricsAllocation *lease, size_t bytes)
{
    bool valid;
    if (metrics == NULL || lease == NULL || bytes == 0 || lease->owner != NULL) { return false; }
    AcquireSRWLockExclusive(&metrics->lock);
    valid = metrics->ledger.owned_allocations != UINT64_MAX &&
        metrics->ledger.owned_bytes <= UINT64_MAX - bytes;
    if (valid) {
        ++metrics->ledger.owned_allocations;
        metrics->ledger.owned_bytes += bytes;
        if (metrics->ledger.owned_bytes > metrics->ledger.owned_high_water) {
            metrics->ledger.owned_high_water = metrics->ledger.owned_bytes;
        }
        *lease = (MetricsAllocation){metrics, bytes};
    } else { ++metrics->ledger.errors; }
    ReleaseSRWLockExclusive(&metrics->lock);
    return valid;
}

bool metrics_allocation_release(MetricsAllocation *lease)
{
    Metrics *metrics;
    bool valid;
    if (lease == NULL || lease->owner == NULL) { return false; }
    metrics = lease->owner;
    AcquireSRWLockExclusive(&metrics->lock);
    valid = metrics->ledger.owned_allocations != 0 && metrics->ledger.owned_bytes >= lease->bytes;
    if (valid) {
        --metrics->ledger.owned_allocations;
        metrics->ledger.owned_bytes -= lease->bytes;
        *lease = (MetricsAllocation){0};
    } else { ++metrics->ledger.errors; }
    ReleaseSRWLockExclusive(&metrics->lock);
    return valid;
}

bool metrics_handle_acquire(Metrics *metrics, MetricsHandle *lease, uintptr_t value)
{
    bool valid;
    if (metrics == NULL || lease == NULL || lease->owner != NULL) { return false; }
    AcquireSRWLockExclusive(&metrics->lock);
    valid = add(&metrics->ledger.handles, 1);
    if (valid) {
        *lease = (MetricsHandle){metrics, value};
        if (metrics->ledger.handles > metrics->ledger.handle_high_water) {
            metrics->ledger.handle_high_water = metrics->ledger.handles;
        }
    } else { ++metrics->ledger.errors; }
    ReleaseSRWLockExclusive(&metrics->lock);
    return valid;
}

bool metrics_handle_release(MetricsHandle *lease)
{
    Metrics *metrics;
    bool valid;
    if (lease == NULL || lease->owner == NULL) { return false; }
    metrics = lease->owner;
    AcquireSRWLockExclusive(&metrics->lock);
    valid = metrics->ledger.handles != 0;
    if (valid) { --metrics->ledger.handles; *lease = (MetricsHandle){0}; }
    else { ++metrics->ledger.errors; }
    ReleaseSRWLockExclusive(&metrics->lock);
    return valid;
}

bool metrics_arena_prepare(Metrics *metrics, MetricsArena *binding)
{
    if (metrics == NULL || binding == NULL || binding->active) { return false; }
    *binding = (MetricsArena){.owner = metrics};
    return true;
}

bool metrics_attach_arena(Metrics *metrics, Arena *arena, MetricsArena *binding)
{
    if (arena == NULL || arena->generation == 0 || arena->used != 0 || arena->high_water != 0 ||
        arena->accounting != NULL ||
        !metrics_arena_prepare(metrics, binding)) { return false; }
    arena_set_accounting(arena, metrics_arena_hook, binding);
    return true;
}

static bool replace(uint64_t *total, uint64_t old, uint64_t current)
{
    if (*total < old || *total - old > UINT64_MAX - current) { return false; }
    *total = *total - old + current;
    return true;
}

void metrics_arena_hook(void *context, const Arena *arena, ArenaEvent event, size_t bytes)
{
    MetricsArena *binding = context;
    Metrics *metrics = binding->owner;
    uint64_t used = arena->used, capacity = arena->capacity;
    uint64_t reserved = binding->reserved, committed = binding->committed;
    bool ending = event == ARENA_EVENT_DISPOSE || event == ARENA_EVENT_RELEASE;
    AcquireSRWLockExclusive(&metrics->lock);
    if (!binding->active) {
        binding->active = true;
        if (!add(&metrics->ledger.arena_count, 1)) { ++metrics->ledger.errors; }
    }
    if (event == ARENA_EVENT_RESERVE) { reserved = bytes; }
    if (event == ARENA_EVENT_COMMIT) { committed = capacity; }
    if (ending) { used = capacity = reserved = committed = 0; }
    if (!replace(&metrics->ledger.arena_used, binding->used, used) ||
        !replace(&metrics->ledger.arena_capacity, binding->capacity, capacity) ||
        !replace(&metrics->ledger.reserved_bytes, binding->reserved, reserved) ||
        !replace(&metrics->ledger.committed_vm_bytes, binding->committed, committed)) {
        ++metrics->ledger.errors;
    }
    binding->used = used; binding->capacity = capacity;
    binding->reserved = reserved; binding->committed = committed;
    if (metrics->ledger.arena_used > metrics->ledger.allocation_high_water) {
        metrics->ledger.allocation_high_water = metrics->ledger.arena_used;
    }
    if (event == ARENA_EVENT_ALLOC_FAILED &&
        !add(&metrics->ledger.allocation_failures, 1)) { ++metrics->ledger.errors; }
    if (ending) { binding->active = false; --metrics->ledger.arena_count; }
    ReleaseSRWLockExclusive(&metrics->lock);
}

Metrics *metrics_arena_owner(const Arena *arena)
{
    if (arena == NULL || arena->accounting != metrics_arena_hook) { return NULL; }
    return ((MetricsArena *)arena->accounting_context)->owner;
}

void metrics_count(Metrics *metrics, MetricCounter counter, uint64_t amount)
{
    uint64_t previous;
    if (metrics == NULL || (unsigned)counter >= METRIC_COUNTER_COUNT) { return; }
    previous = atomic_fetch_add_explicit(&metrics->counters[counter], amount, memory_order_relaxed);
    if (previous > UINT64_MAX - amount) {
        atomic_store_explicit(&metrics->overflow, true, memory_order_relaxed);
    }
}

MetricsSnapshot metrics_snapshot(Metrics *metrics)
{
    MetricsSnapshot snapshot = {0};
    if (metrics == NULL) { snapshot.errors = 1; return snapshot; }
    AcquireSRWLockShared(&metrics->lock);
    snapshot = metrics->ledger;
    ReleaseSRWLockShared(&metrics->lock);
    snapshot.wake_count = atomic_load(&metrics->counters[METRIC_WAKE]);
    snapshot.render_count = atomic_load(&metrics->counters[METRIC_RENDER]);
    snapshot.underruns = atomic_load(&metrics->counters[METRIC_UNDERRUN]);
    snapshot.log_dropped = atomic_load(&metrics->counters[METRIC_LOG_DROP]);
    snapshot.log_written = atomic_load(&metrics->counters[METRIC_LOG_WRITTEN]);
    snapshot.log_write_failed = atomic_load(&metrics->counters[METRIC_LOG_WRITE_FAILED]);
    snapshot.signals = atomic_load(&metrics->counters[METRIC_SIGNAL]);
    snapshot.counter_overflow = atomic_load(&metrics->overflow);
    return snapshot;
}

bool metrics_balanced(Metrics *metrics)
{
    MetricsSnapshot snapshot = metrics_snapshot(metrics);
    return snapshot.arena_count == 0 && snapshot.arena_used == 0 && snapshot.arena_capacity == 0 &&
        snapshot.reserved_bytes == 0 && snapshot.committed_vm_bytes == 0 &&
        snapshot.owned_allocations == 0 && snapshot.owned_bytes == 0 && snapshot.handles == 0 &&
        snapshot.errors == 0 && !snapshot.counter_overflow;
}
