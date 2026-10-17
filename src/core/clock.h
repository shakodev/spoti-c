#ifndef SPOTIC_CORE_CLOCK_H
#define SPOTIC_CORE_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define CORE_CLOCK_NEVER UINT64_MAX

/* Milliseconds in a monotonic domain, not UTC. Injected readers must be cheap,
 * thread-safe, nonblocking, and never move backwards. NULL reader uses Win32. */
typedef struct CoreClock {
    uint64_t (*read_ms)(void *context);
    void *context;
} CoreClock;

uint64_t core_clock_now(const CoreClock *clock);
uint64_t core_deadline_after(uint64_t now, uint64_t delay_ms);
bool core_deadline_expired(uint64_t now, uint64_t deadline);
/* Win32 timeout: UINT32_MAX is infinite; long finite waits are clamped below it. */
uint32_t core_deadline_wait(uint64_t now, uint64_t deadline);
uint64_t core_backoff_ms(uint64_t base_ms, uint64_t cap_ms, uint32_t attempt);

#endif
