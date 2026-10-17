#include "fixture_clock.h"

static uint64_t read_clock(void *context)
{
    return atomic_load_explicit(&((FixtureClock *)context)->now, memory_order_acquire);
}

void fixture_clock_init(FixtureClock *clock, uint64_t initial_ms)
{
    atomic_init(&clock->now, initial_ms);
}

CoreClock fixture_clock_source(FixtureClock *clock)
{
    return (CoreClock){read_clock, clock};
}

bool fixture_clock_advance(FixtureClock *clock, uint64_t delta_ms)
{
    uint64_t current;
    if (clock == NULL) { return false; }
    current = atomic_load_explicit(&clock->now, memory_order_acquire);
    for (;;) {
        if (current > CORE_CLOCK_NEVER - 1 || delta_ms > CORE_CLOCK_NEVER - 1 - current) {
            return false;
        }
        if (atomic_compare_exchange_weak_explicit(&clock->now, &current, current + delta_ms,
            memory_order_acq_rel, memory_order_acquire)) { return true; }
    }
}
