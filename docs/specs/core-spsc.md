# Bounded SPSC handoff

Issue #2 adds a C17 single-producer/single-consumer ring for opaque fixed-size
records or complete interleaved PCM frames. Storage belongs to the caller. The
module has no allocation, OS handles, locks, waits, wake callbacks, or growth.

## Ownership and capacity

`spsc_init` takes a caller-owned buffer, its byte size, a power-of-two element
capacity, and an element stride in bytes. All slots are usable. The stride need
not be a power of two: packed 24-bit stereo frames have a six-byte stride.
Initialization checks multiplication/address overflow, available backing bytes,
and lock-free counter support before changing the object. The caller supplies a
real writable buffer that outlives both endpoints and does not overlap metadata.
The `SpscRing` object itself requires 64-byte alignment; it can be static, on the
stack, or allocated from an arena with `_Alignof(SpscRing)` alignment. Copying a
live ring is forbidden.

Exactly one producer calls `spsc_write`/`spsc_writable`, and exactly one consumer
calls `spsc_read`/`spsc_readable`. Metadata remains immutable while they run. Counts
are endpoint-local snapshots, not a third-thread monitoring interface. Different
producer/consumer threads may not call the same endpoint concurrently; WinHTTP
callbacks with multiple producers need a different transport.

Transfers return the number of whole elements copied. A short write is explicit
backpressure: the untransferred tail remains with the producer, and unread data
is never overwritten. A short read leaves the destination tail untouched; an
audio consumer can explicitly fill that tail with silence. Zero requests or NULL
pointers return zero without touching storage or cursors. Source/destination
buffers must be disjoint from ring metadata/storage and large enough for the
maximum possible transferred prefix. No PCM sample or frame is split between
publication events.

`spsc_init_pcm` selects the largest power-of-two frame count within both the
provided storage and the default **256 KiB** `MEMORY_BUDGET_PCM` limit. Stereo
float frames (eight bytes) fit 32,768 frames; six-byte frames fit 32,768 frames
using 192 KiB. Trailing bytes are unused. Generic command rings use explicit
caller-selected capacities; command payloads should be fixed-size handles and
generations, not borrowed pointers with untracked lifetimes.

## Memory ordering and rollover

The unsigned 64-bit read and write sequences occupy separate 64-byte cache
lines, separate from immutable metadata. The producer loads its write sequence
relaxed and the consumer's read sequence with acquire semantics. It copies into
only free slots, then release-stores the new write sequence. The consumer's
acquire-load of that publication makes both wrapped copies visible before it
reads the elements. The consumer release-stores its read sequence only after
finishing both copies. The producer's acquire-load of that sequence ensures
those reads finish before a slot is reused. Each endpoint is the sole writer
of its sequence, so its own sequence loads need only relaxed ordering.

The invariant is `0 <= (write - read) modulo 2^64 <= capacity`. Capacity is a
power of two below 2^63 and total bytes fit `PTRDIFF_MAX`. Unsigned additions and
subtractions intentionally wrap; masking with `capacity - 1` still selects the
correct slot across rollover because capacity divides 2^64. Requested counts
are clamped before any byte multiplication or pointer arithmetic. Each transfer
uses at most two `memcpy` calls and one release publication, with no retry loop.

The implementation uses `<stdatomic.h>` acquire/release operations. The CMake
build enables `/experimental:c11atomics` for Microsoft's C compiler; clang-cl
uses its native C atomics. `spsc_init` rejects counters for which
`atomic_is_lock_free` is false, and tests verify lock freedom on the actual x64
counter objects. No C++ translation units or C++ standard-library runtime are
introduced.

## Quiescence and wakeups

There is no live reset API. To flush, seek, reinitialize, or return storage to
an arena, first stop both endpoints and obtain both acknowledgements of
quiescence, or join both threads. Only then may the owner reinitialize the ring.
A UI thread cannot reset an active WASAPI consumer's cursors. The ring does not
itself coordinate this external lifecycle protocol.

Notification remains the owning subsystem's responsibility. Producers publish
before signaling; consumers release capacity before notifying a waiting
producer. The surrounding wait protocol must avoid lost wakes. A real audio
callback performs a bounded read and silence fill, while a worker waits on
commands/data availability. Busy retries in the test stress harness do not
define production scheduling.

## Evidence and validation

References consulted without copying or modifying upstream code:

- `ref/librespot/audio/src/fetch/mod.rs`: acquire/release read-position state,
  download status coordination, and streaming commands. Its mutexes and
  unbounded channels are reference behavior, not this ring's implementation.
- `vendor/miniaudio.h` around line 24968: WASAPI transfers whole frames and
  waits on a playback event when the device cannot accept more frames.
- The pinned roadmap and transport/playback specification require SPSC only
  for unique endpoints and acknowledged quiescence before audio-ring reuse.

`core_spsc` covers invalid/overflow initialization, unaligned backing storage,
empty/full states, capacity one, all-slot use, partial and two-copy transfers,
untouched destination tails, explicit backpressure, `SIZE_MAX` requests, and
unsigned sequence rollover. PCM schedules cover 44.1/48 kHz with four-, six-,
eight-, and twelve-byte frames and verify every frame byte.

Two concurrent runs exchange three million command records each, using unequal
batch sizes and checking sequence, handle, generation, and complementary payload
fields. One run starts just before uint64 rollover. Endpoints join before a
reinitialization test. The fixture closes all three Windows handles per run.
Debug MSVC tests use a thread-local CRT allocation watch around ring operations
on both threads and assert zero allocation/free calls, including full/empty paths.

The default Debug/ASan suite includes this ordering stress alongside the arena
and smoke tests. It requires no account, device, or GPU and is a correctness
test rather than a performance benchmark. ASan checks buffer safety; the
ordering argument and stress test provide separate evidence, and stress alone
does not prove the absence of every possible race.
