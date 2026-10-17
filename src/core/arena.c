#include "arena.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string.h>

#if defined(SPOTIC_ASAN)
#include <sanitizer/asan_interface.h>
#define POISON(p, n) __asan_poison_memory_region((p), (n))
#define UNPOISON(p, n) __asan_unpoison_memory_region((p), (n))
#else
#define POISON(p, n) ((void)(p), (void)(n))
#define UNPOISON(p, n) ((void)(p), (void)(n))
#endif

static void account(const Arena *arena, ArenaEvent event, size_t bytes)
{
    if (arena->accounting != NULL) {
        arena->accounting(arena->accounting_context, arena, event, bytes);
    }
}

bool arena_init(Arena *arena, void *memory, size_t capacity)
{
    if (arena == NULL || (memory == NULL && capacity != 0) ||
        capacity > (size_t)PTRDIFF_MAX ||
        capacity > UINTPTR_MAX - (uintptr_t)memory) {
        return false;
    }
    *arena = (Arena){.base = memory, .capacity = capacity, .generation = 1};
    if (capacity != 0) {
        POISON(memory, capacity);
    }
    return true;
}

void arena_set_accounting(Arena *arena, ArenaAccountingHook hook, void *context)
{
    if (arena != NULL && arena->generation != 0) {
        arena->accounting = hook;
        arena->accounting_context = context;
        account(arena, ARENA_EVENT_SNAPSHOT, 0);
    }
}

void *arena_alloc(Arena *arena, size_t bytes, size_t alignment)
{
    uintptr_t address;
    size_t padding;
    size_t remaining;
    size_t end;
    void *result;

    if (arena == NULL || arena->generation == 0) {
        return NULL;
    }
    if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) {
        account(arena, ARENA_EVENT_ALLOC_FAILED, bytes);
        return NULL;
    }
    /* The initialized range fits uintptr_t; subtraction checks precede all
     * pointer arithmetic and addition of the requested size or padding. */
    remaining = arena->capacity - arena->used;
    address = (uintptr_t)arena->base + arena->used;
    padding = (size_t)((0 - address) & (alignment - 1));
    if (padding > remaining || bytes > remaining - padding) {
        account(arena, ARENA_EVENT_ALLOC_FAILED, bytes);
        return NULL;
    }
    end = arena->used + padding + bytes;
    result = arena->base + arena->used + padding;
    UNPOISON(arena->base + arena->used, end - arena->used);
    arena->used = end;
    if (end > arena->high_water) {
        arena->high_water = end;
    }
    account(arena, ARENA_EVENT_ALLOC, bytes);
    return result;
}

ArenaMark arena_mark(const Arena *arena)
{
    if (arena == NULL || arena->generation == 0) {
        return (ArenaMark){0};
    }
    return (ArenaMark){arena, arena->used, arena->generation};
}

bool arena_rewind(Arena *arena, ArenaMark mark)
{
    size_t reclaimed;
    if (arena == NULL || arena->generation == 0 || mark.owner != arena ||
        mark.generation != arena->generation || mark.used > arena->used) {
        return false;
    }
    reclaimed = arena->used - mark.used;
    if (reclaimed != 0) {
        POISON(arena->base + mark.used, reclaimed);
    }
    arena->used = mark.used;
    account(arena, ARENA_EVENT_REWIND, reclaimed);
    return true;
}

bool arena_reset(Arena *arena)
{
    size_t reclaimed;
    if (arena == NULL || arena->generation == 0 || arena->generation == UINT64_MAX) {
        return false;
    }
    reclaimed = arena->used;
    if (reclaimed != 0) {
        POISON(arena->base, reclaimed);
    }
    arena->used = 0;
    ++arena->generation;
    account(arena, ARENA_EVENT_RESET, reclaimed);
    return true;
}

void arena_dispose(Arena *arena)
{
    if (arena != NULL && arena->generation != 0) {
        if (arena->capacity != 0) {
            UNPOISON(arena->base, arena->capacity);
        }
        account(arena, ARENA_EVENT_DISPOSE, arena->capacity);
        *arena = (Arena){0};
    }
}

static bool page_round(size_t bytes, size_t *rounded)
{
    SYSTEM_INFO info;
    size_t remainder;
    size_t padding;
    GetSystemInfo(&info);
    remainder = bytes % info.dwPageSize;
    padding = remainder == 0 ? 0 : info.dwPageSize - remainder;
    if (bytes > (size_t)PTRDIFF_MAX - padding) {
        return false;
    }
    *rounded = bytes + padding;
    return true;
}

bool arena_vm_reserve(ArenaVm *vm, size_t bytes,
                      ArenaAccountingHook hook, void *context)
{
    size_t reserved;
    void *memory;
    if (vm == NULL || vm->arena.base != NULL || vm->reserved != 0 || bytes == 0 ||
        !page_round(bytes, &reserved)) {
        return false;
    }
    memory = VirtualAlloc(NULL, reserved, MEM_RESERVE, PAGE_NOACCESS);
    if (memory == NULL) {
        return false;
    }
    /* reserved is page-rounded and <= PTRDIFF_MAX, and VirtualAlloc returned a
     * real range. Initialization of this zero-capacity view cannot fail. */
    (void)arena_init(&vm->arena, memory, 0);
    vm->reserved = reserved;
    arena_set_accounting(&vm->arena, hook, context);
    account(&vm->arena, ARENA_EVENT_RESERVE, reserved);
    return true;
}

bool arena_vm_commit(ArenaVm *vm, size_t total_bytes)
{
    size_t capacity;
    size_t delta;
    uint8_t *start;
    if (vm == NULL || vm->reserved == 0 ||
        !page_round(total_bytes, &capacity) || capacity > vm->reserved) {
        return false;
    }
    if (capacity <= vm->arena.capacity) {
        return true;
    }
    delta = capacity - vm->arena.capacity;
    start = vm->arena.base + vm->arena.capacity;
    if (VirtualAlloc(start, delta, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        return false;
    }
    POISON(start, delta);
    vm->arena.capacity = capacity;
    account(&vm->arena, ARENA_EVENT_COMMIT, delta);
    return true;
}

bool arena_vm_release(ArenaVm *vm)
{
    if (vm == NULL || vm->reserved == 0) {
        return false;
    }
    if (vm->arena.capacity != 0) {
        UNPOISON(vm->arena.base, vm->arena.capacity);
    }
    if (!VirtualFree(vm->arena.base, 0, MEM_RELEASE)) {
        if (vm->arena.capacity != vm->arena.used) {
            POISON(vm->arena.base + vm->arena.used,
                   vm->arena.capacity - vm->arena.used);
        }
        return false;
    }
    account(&vm->arena, ARENA_EVENT_RELEASE, vm->reserved);
    *vm = (ArenaVm){0};
    return true;
}
