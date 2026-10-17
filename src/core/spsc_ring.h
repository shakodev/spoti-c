#ifndef SPOTIC_CORE_SPSC_RING_H
#define SPOTIC_CORE_SPSC_RING_H

#include "memory_budget.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define SPSC_CACHE_LINE ((size_t)64)
#define SPSC_PCM_DEFAULT_BYTES MEMORY_BUDGET_PCM

typedef struct SpscRing {
    uint8_t *storage;
    size_t capacity; /* Power-of-two element count, not bytes. All slots usable. */
    size_t stride;   /* Bytes per element; PCM uses an entire interleaved frame. */
    uint8_t metadata_padding[64 - sizeof(uint8_t *) - 2 * sizeof(size_t)];
    _Alignas(64) _Atomic uint64_t read_count;
    uint8_t read_padding[64 - sizeof(_Atomic uint64_t)];
    _Alignas(64) _Atomic uint64_t write_count;
    uint8_t write_padding[64 - sizeof(_Atomic uint64_t)];
} SpscRing;

_Static_assert(_Alignof(SpscRing) >= 64, "Ring counters require cache alignment");
_Static_assert(offsetof(SpscRing, read_count) % 64 == 0, "Read counter alignment");
_Static_assert(offsetof(SpscRing, write_count) - offsetof(SpscRing, read_count) >= 64,
               "Counters must occupy separate cache lines");

/* Exactly one producer owns write/writable, and one consumer owns read/readable.
 * Storage and metadata stay alive and unchanged while either endpoint is active.
 * Do not copy a live ring. SpscRing itself requires its declared 64-byte alignment;
 * backing bytes need no special alignment because transfers use memcpy, but
 * must not overlap the ring metadata.
 *
 * Init/reinit is allowed ONLY before starting endpoints or after BOTH have
 * acknowledged quiescence (e.g. stopped and joined). A UI thread must never
 * reinitialize active audio cursors. There is deliberately no live reset API.
 * Invalid init leaves the object unchanged. No allocation or ownership transfer.
 * capacity * stride must fit storage_bytes and PTRDIFF_MAX. Capacity is < 2^63
 * so unsigned counter subtraction remains bounded across uint64_t rollover.
 * Initialization rejects counters that are not lock-free on the current target. */
bool spsc_init(SpscRing *ring, void *storage, size_t storage_bytes,
               size_t capacity, size_t stride);
/* Choose the largest power-of-two frame count fitting both storage_bytes and
 * the default 256 KiB PCM cap. Unused trailing bytes are never touched. */
bool spsc_init_pcm(SpscRing *ring, void *storage, size_t storage_bytes,
                   size_t frame_stride);
bool spsc_is_lock_free(const SpscRing *ring);

/* Counts and transfer sizes are ELEMENTS/FRAMES, never individual PCM samples
 * or bytes. Short transfers (including zero) are explicit full/empty backpressure.
 * No overwrite, waits, locks, growth, callbacks, or allocation. Zero count or a
 * NULL pointer returns zero unchanged. Buffers must not overlap the ring storage
 * or metadata and must accommodate min(requested, capacity) whole elements.
 * Only the returned prefix is read/written; caller retains any untransferred tail.
 * Call these only on a successfully initialized ring (NULL safely returns zero). */
size_t spsc_write(SpscRing *ring, const void *source, size_t elements);
size_t spsc_read(SpscRing *ring, void *destination, size_t elements);
/* Endpoint-local snapshots: consumer calls readable; producer calls writable.
 * The opposite endpoint can make more data/space available immediately after
 * a snapshot. These are not consistent third-thread monitoring snapshots. */
size_t spsc_readable(const SpscRing *ring);
size_t spsc_writable(const SpscRing *ring);

#endif
