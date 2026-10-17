# Core memory ownership

Issue #1 introduces a C17 fixed-capacity arena with caller-owned storage and an
optional Windows virtual-memory owner. Each arena belongs to one thread; it has
no lock, heap fallback, or implicit capacity growth. `memory_budget.h` records the
initial subsystem caps: 2,560 KiB headless plus 4,864 KiB for the GUI. These are
design limits, not measured resident memory. OS/driver resources and stacks must
also be measured.

## Fixed storage and frame scratch

```c
_Alignas(64) uint8_t storage[4096];
Arena scratch;
if (!arena_init(&scratch, storage, sizeof(storage))) {
    /* Report initialization failure. */
    return;
}
uint8_t *instances = arena_alloc(&scratch, 256, 64);
if (instances == NULL) {
    /* Reject work that exceeds the fixed budget. */
}
/* Consume instances before resetting scratch. */
if (!arena_reset(&scratch)) {
    /* Generation exhausted: retire the arena. */
}
arena_dispose(&scratch);
```

Initialize/dispose outside the render loop. UI-owned scratch resets only after
renderer consumption; asynchronous consumers must finish or copy to their own
storage first. A retained scratch reference carries the saved `generation` and
checks it against the arena before access. Reset increments the generation;
generation exhaustion fails without wrapping. Reset preserves `high_water`,
which includes alignment padding. Allocation memory is uninitialized.

Allocation rejects zero bytes, zero/non-power-of-two alignment, size overflow,
and insufficient capacity without changing arena state. Alignment is relative
to the actual address, including unaligned backing buffers. Invalid initialization
also preserves state. The caller must supply a real writable range of the declared
capacity, keep it alive, and never edit or copy a live arena's fields.

Marks carry arena identity, offset, and generation. Rewind releases a suffix,
preserves outer marks and lifetime high-water, and rejects foreign, future, or
reset-invalidated marks. Discard marks inside a released suffix; they cannot be
reused after subsequent allocations. Marks and references do not survive disposal
and reinitialization at the same address.

ASan builds poison unused and reclaimed storage and unpoison successful
allocations. Allocation padding is accessible. ASan shadow granularity can leave
bytes of an unaligned boundary accessible, and an address reused by a subsequent
allocation is accessible again; generations and lifetime rules still apply.
Dispose unpoisons caller-owned backing storage before returning it to the caller.
Disposal does not free that storage.

## Virtual memory and accounting

Start `ArenaVm` with `{0}`. `arena_vm_reserve` uses `VirtualAlloc(MEM_RESERVE)`
without making pages writable. `arena_vm_commit` explicitly commits a page-rounded
total capacity with `MEM_COMMIT`; it never shrinks. Allocation exhaustion does
not commit more pages. `arena_vm_release` uses `VirtualFree(MEM_RELEASE)` and
clears the owner on success. Use this release function instead of disposing the
embedded arena. Setup, commit, and release belong outside per-frame processing.

An optional synchronous accounting hook observes allocation success/failure,
rewind, reset, and disposal; the VM wrapper also reports reserved, committed,
and released bytes. Attaching a hook sends a snapshot. Hooks read current
usage/capacity/high-water, must not reenter or mutate the arena, and must obey
the calling thread's allocation restrictions. Consumers own their ledger and
handle cumulative-counter overflow. Failed operations may report failure through
the hook but leave the arena's own fields unchanged.

## Validation

`core_arena` covers empty/invalid initialization, zero-size policy, unaligned
storage, alignment through 256 bytes, padding exhaustion, exact exhaustion,
`SIZE_MAX`, nested/foreign/stale marks, reset/reuse, generation exhaustion,
high-water reporting, hook events, and separate OS reserve/commit/release.
Debug MSVC tests install a CRT allocation hook and assert zero heap calls across
these operations, including repeated exhaustion. VM tests query page state and
assert balanced reservation/release counts and bytes over repeated lifetimes.
Windows ASan is not a leak detector; explicit ownership accounting remains required.

The Debug/ASan suite retains `smoke` and adds three isolated misuse fixtures.
Their runner requires both ASan's `use-after-poison` diagnostic and exit code 86;
an unrelated crash or missing instrumentation fails the test. Intentional invalid
accesses exist only in those child fixtures. Release and Debug without ASan do
not register them.

Reference interfaces consulted without modification: Clay's
`Clay_CreateArenaWithCapacityAndMemory` accepts caller-owned capacity/storage;
stb_vorbis's allocation interface accepts a fixed buffer for setup and temporary
memory and reports exhaustion. Neither vendor implementation is compiled into
this module, and no upstream source or license was changed.
