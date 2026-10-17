#ifndef SPOTIC_CORE_ARENA_H
#define SPOTIC_CORE_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Arena Arena;

typedef enum ArenaEvent {
    ARENA_EVENT_SNAPSHOT,
    ARENA_EVENT_ALLOC,
    ARENA_EVENT_ALLOC_FAILED,
    ARENA_EVENT_REWIND,
    ARENA_EVENT_RESET,
    ARENA_EVENT_DISPOSE,
    ARENA_EVENT_RESERVE,
    ARENA_EVENT_COMMIT,
    ARENA_EVENT_RELEASE
} ArenaEvent;

/* Synchronous, owner-thread callback. It must not mutate/reenter the arena.
 * bytes means requested bytes for ALLOC/FAILED, reclaimed bytes for rewind/reset,
 * and OS bytes for reserve/commit/release. Padding is included in arena->used.
 * Read used/capacity/high_water at SNAPSHOT to initialize an external ledger. */
typedef void (*ArenaAccountingHook)(void *context, const Arena *arena,
                                    ArenaEvent event, size_t bytes);

struct Arena {
    uint8_t *base;
    size_t capacity;
    size_t used;
    size_t high_water;
    uint64_t generation;
    ArenaAccountingHook accounting;
    void *accounting_context;
};

typedef struct ArenaMark {
    const Arena *owner;
    size_t used;
    uint64_t generation;
} ArenaMark;

/* One thread owns an arena, its storage, marks and accounting. Never copy a live
 * arena or reinitialize it before disposal. The caller supplies a real writable
 * buffer of capacity bytes; NULL is accepted only for capacity zero. Failed
 * operations leave the arena intact. There is no malloc or automatic growth.
 * Zero-byte allocations and non-power-of-two alignments return NULL unchanged.
 * Initialize/dispose outside rendering. ASan poisons unused storage, so dispose
 * before the caller accesses or frees the backing buffer again. */
bool arena_init(Arena *arena, void *memory, size_t capacity);
void arena_set_accounting(Arena *arena, ArenaAccountingHook hook, void *context);
void *arena_alloc(Arena *arena, size_t bytes, size_t alignment);
ArenaMark arena_mark(const Arena *arena);
/* Rewind preserves outer marks and high_water. Discard marks in the removed
 * suffix; they must not be reused even if later allocations reach that offset. */
bool arena_rewind(Arena *arena, ArenaMark mark);
/* Reset invalidates all marks and scratch references; high_water is lifetime
 * peak including padding. Generation never wraps: exhaustion returns false.
 * UI frame scratch may reset ONLY after the renderer has consumed the frame.
 * Consumers retaining a scratch reference must also check its saved generation
 * before access; ASan alone cannot detect a pointer after its address is reused. */
bool arena_reset(Arena *arena);
void arena_dispose(Arena *arena);

/* Owning VM wrapper. Start with {0}; release before reuse. Reserve address space
 * separately, then explicitly commit a page-rounded total writable capacity.
 * Smaller commits are no-ops; decommit/shrink are intentionally unsupported.
 * Use arena_vm_release, never arena_dispose, for VM-backed arenas. */
typedef struct ArenaVm {
    Arena arena;
    size_t reserved;
} ArenaVm;

bool arena_vm_reserve(ArenaVm *vm, size_t bytes,
                      ArenaAccountingHook hook, void *context);
bool arena_vm_commit(ArenaVm *vm, size_t total_bytes);
bool arena_vm_release(ArenaVm *vm);

#endif
