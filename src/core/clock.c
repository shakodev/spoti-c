#include "clock.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

uint64_t core_clock_now(const CoreClock *clock)
{
    return clock != NULL && clock->read_ms != NULL ?
        clock->read_ms(clock->context) : GetTickCount64();
}

uint64_t core_deadline_after(uint64_t now, uint64_t delay_ms)
{
    /* Reserve UINT64_MAX for infinity; overflow saturates to the last finite ms. */
    if (now >= CORE_CLOCK_NEVER - 1 || delay_ms >= CORE_CLOCK_NEVER - 1 - now) {
        return CORE_CLOCK_NEVER - 1;
    }
    return now + delay_ms;
}

bool core_deadline_expired(uint64_t now, uint64_t deadline)
{
    return deadline != CORE_CLOCK_NEVER && now >= deadline;
}

uint32_t core_deadline_wait(uint64_t now, uint64_t deadline)
{
    uint64_t remaining;
    if (deadline == CORE_CLOCK_NEVER) { return UINT32_MAX; }
    if (now >= deadline) { return 0; }
    remaining = deadline - now;
    return remaining >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)remaining;
}

uint64_t core_backoff_ms(uint64_t base_ms, uint64_t cap_ms, uint32_t attempt)
{
    if (base_ms == 0 || cap_ms == 0) { return 0; }
    if (base_ms >= cap_ms || attempt >= 64 || base_ms > (cap_ms >> attempt)) {
        return cap_ms;
    }
    return base_ms << attempt;
}
