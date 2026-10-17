#include "core/arena.h"
#include "core/memory_budget.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static size_t heap_calls;
static int __cdecl count_heap(int type, void *data, size_t size, int block,
                              long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block;
    (void)request; (void)file; (void)line;
    ++heap_calls;
    return 1;
}
#endif

#if defined(SPOTIC_ASAN)
#include <sanitizer/asan_interface.h>
#endif

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

typedef struct Ledger {
    size_t events[ARENA_EVENT_RELEASE + 1];
    size_t bytes[ARENA_EVENT_RELEASE + 1];
    size_t used;
    size_t peak;
    size_t capacity;
} Ledger;

static void record(void *context, const Arena *arena, ArenaEvent event, size_t bytes)
{
    Ledger *ledger = context;
    ++ledger->events[event];
    ledger->bytes[event] += bytes;
    ledger->used = arena->used;
    ledger->peak = arena->high_water;
    ledger->capacity = arena->capacity;
}

static void unchanged(const Arena *arena, const Arena *before)
{
    CHECK(arena->base == before->base && arena->capacity == before->capacity);
    CHECK(arena->used == before->used && arena->high_water == before->high_water);
    CHECK(arena->generation == before->generation);
    CHECK(arena->accounting == before->accounting);
    CHECK(arena->accounting_context == before->accounting_context);
}

static void test_empty_and_invalid(void)
{
    Arena arena = {0};
    Arena before = arena;
    uint8_t buffer[16];
    CHECK(!arena_init(NULL, buffer, sizeof(buffer)));
    CHECK(!arena_init(&arena, NULL, 1));
    CHECK(!arena_init(&arena, buffer, SIZE_MAX));
    CHECK(!arena_init(&arena, (void *)(uintptr_t)(UINTPTR_MAX - 7), 16));
    unchanged(&arena, &before);
    CHECK(arena_alloc(&arena, 1, 1) == NULL);
    CHECK(!arena_reset(&arena));
    CHECK(!arena_rewind(&arena, arena_mark(&arena)));
    CHECK(arena_init(&arena, NULL, 0));
    before = arena;
    CHECK(arena_alloc(&arena, 0, 1) == NULL);
    CHECK(arena_alloc(&arena, 1, 1) == NULL);
    unchanged(&arena, &before);
    CHECK(arena_reset(&arena));
    arena_dispose(&arena);
    CHECK(arena_init(&arena, buffer, 0));
    CHECK(arena_alloc(&arena, 1, 1) == NULL);
    arena_dispose(&arena);
    CHECK(arena_alloc(NULL, 1, 1) == NULL);
    CHECK(!arena_reset(NULL));
    CHECK(!arena_rewind(NULL, arena_mark(NULL)));
    arena_dispose(NULL);
    arena_dispose(&arena);
    arena_set_accounting(NULL, record, NULL);
}

static void test_alignment(void)
{
    _Alignas(256) uint8_t storage[1024];
    Arena arena;
    for (size_t alignment = 1; alignment <= 256; alignment *= 2) {
        CHECK(arena_init(&arena, storage + 1, sizeof(storage) - 1));
        for (size_t i = 0; i < 3; ++i) {
            size_t old_used = arena.used;
            uint8_t *data = arena_alloc(&arena, 3, alignment);
            CHECK(data != NULL && (uintptr_t)data % alignment == 0);
            CHECK(data >= storage + 1 + old_used);
            CHECK(arena.used == (size_t)(data - arena.base) + 3);
            CHECK(arena.high_water == arena.used);
            memset(data, 0x5a, 3);
        }
        arena_dispose(&arena);
    }
    CHECK(arena_init(&arena, storage, sizeof(storage)));
    const size_t bad_alignments[] = {0, 3, 7, 63, SIZE_MAX};
    for (size_t i = 0; i < sizeof(bad_alignments) / sizeof(bad_alignments[0]); ++i) {
        Arena before = arena;
        CHECK(arena_alloc(&arena, 1, bad_alignments[i]) == NULL);
        unchanged(&arena, &before);
    }
    Arena before = arena;
    CHECK(arena_alloc(&arena, SIZE_MAX, 64) == NULL);
    CHECK(arena_alloc(&arena, 1, (SIZE_MAX / 2) + 1) == NULL);
    unchanged(&arena, &before);
    arena_dispose(&arena);
    /* The payload fits, but alignment padding exhausts the unaligned buffer. */
    CHECK(arena_init(&arena, storage + 1, 63));
    before = arena;
    CHECK(arena_alloc(&arena, 1, 64) == NULL);
    CHECK(!arena_init(&arena, NULL, 1));
    unchanged(&arena, &before);
    arena_dispose(&arena);
    CHECK(arena_init(&arena, storage + 1, 64));
    CHECK(arena_alloc(&arena, 1, 64) == storage + 64);
    CHECK(arena.used == 64 && arena.high_water == 64);
    arena_dispose(&arena);
}

static void test_exhaustion_and_accounting(void)
{
    _Alignas(64) uint8_t storage[128];
    Arena arena;
    Ledger ledger = {0};
    CHECK(arena_init(&arena, storage, sizeof(storage)));
    arena_set_accounting(&arena, record, &ledger);
    CHECK(ledger.events[ARENA_EVENT_SNAPSHOT] == 1);
    CHECK(ledger.capacity == sizeof(storage));
    CHECK(arena_alloc(&arena, 1, 1) == storage);
    CHECK(arena_alloc(&arena, 64, 64) == storage + 64);
    CHECK(arena.used == 128 && arena.high_water == 128);
    memset(storage, 0x35, sizeof(storage));
    Arena before = arena;
    for (size_t i = 0; i < 1000; ++i) {
        CHECK(arena_alloc(&arena, 1, 1) == NULL);
        CHECK(arena_alloc(&arena, SIZE_MAX, 64) == NULL);
        unchanged(&arena, &before);
    }
    CHECK(arena_alloc(&arena, 0, 64) == NULL);
    unchanged(&arena, &before);
    CHECK(ledger.events[ARENA_EVENT_ALLOC] == 2);
    CHECK(ledger.events[ARENA_EVENT_ALLOC_FAILED] == 2001);
    CHECK(ledger.bytes[ARENA_EVENT_ALLOC] == 65);
    CHECK(ledger.used == 128 && ledger.peak == 128);
    CHECK(storage[0] == 0x35 && storage[127] == 0x35);
    CHECK(arena_reset(&arena));
    CHECK(ledger.bytes[ARENA_EVENT_RESET] == 128 && ledger.used == 0);
    CHECK(arena.high_water == 128);
    CHECK(arena_alloc(&arena, sizeof(storage), 1) == storage);
    memset(storage, 0x42, sizeof(storage));
    arena_dispose(&arena);
    CHECK(ledger.events[ARENA_EVENT_DISPOSE] == 1);
    memset(storage, 0, sizeof(storage)); /* Disposal returns storage to caller. */
    CHECK(arena.base == NULL && arena.capacity == 0 && arena.generation == 0);
}

static void test_marks_and_generation(void)
{
    _Alignas(64) uint8_t storage[256];
    uint8_t other_storage[16];
    Arena arena, other;
    Ledger ledger = {0};
    CHECK(arena_init(&arena, storage, sizeof(storage)));
    CHECK(arena_init(&other, other_storage, sizeof(other_storage)));
    arena_set_accounting(&arena, record, &ledger);
    ArenaMark outer = arena_mark(&arena);
    uint8_t *first = arena_alloc(&arena, 32, 1);
    CHECK(first != NULL);
    memset(first, 1, 32);
    ArenaMark inner = arena_mark(&arena);
    CHECK(arena_alloc(&arena, 32, 64) == storage + 64);
    ArenaMark tail = arena_mark(&arena);
    CHECK(arena_rewind(&arena, inner));
    CHECK(arena.used == 32 && arena.high_water == 96 && first[0] == 1);
    CHECK(ledger.bytes[ARENA_EVENT_REWIND] == 64);
    Arena before = arena;
    CHECK(!arena_rewind(&arena, tail));
    CHECK(!arena_rewind(&arena, arena_mark(&other)));
    CHECK(!arena_rewind(&arena, (ArenaMark){0}));
    unchanged(&arena, &before);
    CHECK(arena_alloc(&arena, 32, 64) == storage + 64);
    CHECK(arena_rewind(&arena, inner));
    CHECK(arena_rewind(&arena, outer));
    CHECK(arena.used == 0 && arena.high_water == 96);
    CHECK(arena_rewind(&arena, outer));
    uint64_t old_generation = arena.generation;
    CHECK(arena_reset(&arena));
    CHECK(arena.generation == old_generation + 1);
    before = arena;
    CHECK(!arena_rewind(&arena, outer));
    unchanged(&arena, &before);
    CHECK(arena_alloc(&arena, 8, 1) == first);
    CHECK(arena.generation != old_generation); /* Detect reference after reuse. */
    arena.generation = UINT64_MAX;
    before = arena;
    CHECK(!arena_reset(&arena));
    unchanged(&arena, &before);
    arena_dispose(&other);
    arena_dispose(&arena);
}

static void test_vm(void)
{
    SYSTEM_INFO info;
    MEMORY_BASIC_INFORMATION region;
    ArenaVm vm = {0};
    Ledger ledger = {0};
    GetSystemInfo(&info);
    size_t page = info.dwPageSize;
    CHECK(!arena_vm_reserve(NULL, page, NULL, NULL));
    CHECK(!arena_vm_reserve(&vm, 0, NULL, NULL));
    CHECK(!arena_vm_reserve(&vm, SIZE_MAX, NULL, NULL));
    CHECK(!arena_vm_commit(&vm, 1) && !arena_vm_release(&vm));
    CHECK(!arena_vm_commit(NULL, 1) && !arena_vm_release(NULL));
    CHECK(arena_vm_reserve(&vm, 3 * page - 1, record, &ledger));
    CHECK(vm.reserved == 3 * page && vm.arena.capacity == 0);
    CHECK(ledger.bytes[ARENA_EVENT_RESERVE] == vm.reserved);
    CHECK(VirtualQuery(vm.arena.base, &region, sizeof(region)) == sizeof(region));
    CHECK(region.State == MEM_RESERVE);
    Arena before = vm.arena;
    CHECK(arena_alloc(&vm.arena, 1, 1) == NULL);
    CHECK(!arena_vm_reserve(&vm, page, NULL, NULL));
    unchanged(&vm.arena, &before);
    CHECK(arena_vm_commit(&vm, 0));
    CHECK(arena_vm_commit(&vm, 1));
    CHECK(vm.arena.capacity == page);
    CHECK(VirtualQuery(vm.arena.base, &region, sizeof(region)) == sizeof(region));
    CHECK(region.State == MEM_COMMIT && region.Protect == PAGE_READWRITE);
    CHECK(VirtualQuery(vm.arena.base + page, &region, sizeof(region)) == sizeof(region));
    CHECK(region.State == MEM_RESERVE);
    uint8_t *data = arena_alloc(&vm.arena, page, 1);
    CHECK(data == vm.arena.base);
    memset(data, 0x71, page);
    CHECK(arena_alloc(&vm.arena, 1, 1) == NULL);
    CHECK(vm.arena.capacity == page); /* Exhaustion cannot implicitly commit. */
    ArenaMark mark = arena_mark(&vm.arena);
    CHECK(arena_vm_commit(&vm, page + 1));
    CHECK(vm.arena.capacity == 2 * page && vm.arena.high_water == page);
    CHECK(data[0] == 0x71 && data[page - 1] == 0x71);
    CHECK(arena_alloc(&vm.arena, page, 1) == data + page);
    CHECK(arena_rewind(&vm.arena, mark));
    CHECK(arena_vm_commit(&vm, vm.reserved));
    before = vm.arena;
    CHECK(arena_vm_commit(&vm, 1));
    CHECK(!arena_vm_commit(&vm, vm.reserved + 1));
    CHECK(!arena_vm_commit(&vm, SIZE_MAX));
    unchanged(&vm.arena, &before);
    CHECK(ledger.bytes[ARENA_EVENT_COMMIT] == vm.reserved);
    CHECK(arena_vm_release(&vm));
    CHECK(vm.arena.base == NULL && vm.reserved == 0);
    CHECK(ledger.bytes[ARENA_EVENT_RELEASE] == ledger.bytes[ARENA_EVENT_RESERVE]);
    CHECK(!arena_vm_release(&vm));
    for (size_t i = 0; i < 64; ++i) {
        CHECK(arena_vm_reserve(&vm, page, record, &ledger));
        CHECK(arena_vm_commit(&vm, page));
        CHECK(arena_alloc(&vm.arena, page, 1) != NULL);
        CHECK(arena_vm_release(&vm));
    }
    CHECK(ledger.events[ARENA_EVENT_RESERVE] == ledger.events[ARENA_EVENT_RELEASE]);
    CHECK(ledger.bytes[ARENA_EVENT_RESERVE] == ledger.bytes[ARENA_EVENT_RELEASE]);
}

static void test_poison(void)
{
#if defined(SPOTIC_ASAN)
    _Alignas(64) uint8_t storage[128];
    Arena arena;
    CHECK(arena_init(&arena, storage, sizeof(storage)));
    CHECK(__asan_address_is_poisoned(storage));
    uint8_t *data = arena_alloc(&arena, 32, 1);
    CHECK(data == storage && !__asan_address_is_poisoned(data + 31));
    CHECK(__asan_address_is_poisoned(data + 32));
    ArenaMark mark = arena_mark(&arena);
    CHECK(arena_alloc(&arena, 32, 1) == storage + 32);
    CHECK(arena_rewind(&arena, mark));
    CHECK(!__asan_address_is_poisoned(data + 31));
    CHECK(__asan_address_is_poisoned(data + 32));
    CHECK(arena_reset(&arena));
    CHECK(__asan_address_is_poisoned(data));
    CHECK(arena_alloc(&arena, 32, 1) == data);
    CHECK(!__asan_address_is_poisoned(data));
    arena_dispose(&arena);
    CHECK(!__asan_address_is_poisoned(storage + 127));
#endif
}

/* Intentional invalid accesses live only in child fixtures. The CMake runner
 * requires an ASan use-after-poison report, not merely a failing exit status. */
static int misuse(const char *mode)
{
#if defined(SPOTIC_ASAN)
    _Alignas(64) uint8_t storage[128];
    Arena arena;
    CHECK(arena_init(&arena, storage, sizeof(storage)));
    CHECK(arena_alloc(&arena, 32, 1) == storage);
    ArenaMark mark = arena_mark(&arena);
    volatile uint8_t *stale = arena_alloc(&arena, 32, 1);
    CHECK(stale != NULL);
    if (strcmp(mode, "reset") == 0) {
        CHECK(arena_reset(&arena));
    } else if (strcmp(mode, "rewind") == 0) {
        CHECK(arena_rewind(&arena, mark));
    } else if (strcmp(mode, "overrun") == 0) {
        stale += 32;
    } else {
        return 2;
    }
    *stale = 0x42;
    arena_dispose(&arena);
#else
    (void)mode;
#endif
    return 3; /* The runner rejects a fixture that reaches here. */
}

int main(int argc, char **argv)
{
    if (argc == 2) {
        return misuse(argv[1]);
    }
#if defined(_MSC_VER) && defined(_DEBUG)
    _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(count_heap);
#endif
    test_empty_and_invalid();
    test_alignment();
    test_exhaustion_and_accounting();
    test_marks_and_generation();
    test_vm();
    test_poison();
    CHECK(MEMORY_BUDGET_TOTAL == 7424 * MEMORY_KIB);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(previous);
    CHECK(heap_calls == 0); /* Includes all failure paths: no fallback malloc. */
#endif
    puts("Arena boundaries, generations, accounting and VM ownership passed");
    return 0;
}
