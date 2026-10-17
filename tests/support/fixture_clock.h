#ifndef SPOTIC_FIXTURE_CLOCK_H
#define SPOTIC_FIXTURE_CLOCK_H

#include "core/clock.h"
#include <stdatomic.h>

typedef struct FixtureClock { _Atomic uint64_t now; } FixtureClock;
void fixture_clock_init(FixtureClock *clock, uint64_t initial_ms);
CoreClock fixture_clock_source(FixtureClock *clock);
/* Thread-safe, monotonic, checked advance. No sleeps or implicit worker wakes. */
bool fixture_clock_advance(FixtureClock *clock, uint64_t delta_ms);

#endif
