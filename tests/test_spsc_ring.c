#include "core/spsc_ring.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static _Atomic size_t heap_calls;
static _Thread_local bool watch_heap;
static int __cdecl count_heap(int type, void *data, size_t size, int block,
                              long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block;
    (void)request; (void)file; (void)line;
    if (watch_heap) {
        atomic_fetch_add_explicit(&heap_calls, 1, memory_order_relaxed);
    }
    return 1;
}
#define HEAP_WATCH(value) (watch_heap = (value))
#else
#define HEAP_WATCH(value) ((void)(value))
#endif

typedef struct Snapshot {
    uint8_t *storage;
    size_t capacity, stride;
    uint64_t read, write;
} Snapshot;

static Snapshot snapshot(const SpscRing *ring)
{
    return (Snapshot){ring->storage, ring->capacity, ring->stride,
        atomic_load(&ring->read_count), atomic_load(&ring->write_count)};
}

static void unchanged(const SpscRing *ring, Snapshot before)
{
    CHECK(ring->storage == before.storage && ring->capacity == before.capacity);
    CHECK(ring->stride == before.stride);
    CHECK(atomic_load(&ring->read_count) == before.read);
    CHECK(atomic_load(&ring->write_count) == before.write);
}

static void test_init(void)
{
    SpscRing ring = {0};
    uint8_t storage[32];
    CHECK(spsc_init(&ring, storage + 1, 31, 8, 3));
    CHECK(spsc_is_lock_free(&ring));
    CHECK((uintptr_t)&ring.read_count % SPSC_CACHE_LINE == 0);
    CHECK((uintptr_t)&ring.write_count % SPSC_CACHE_LINE == 0);
    CHECK((uintptr_t)&ring.write_count - (uintptr_t)&ring.read_count >= SPSC_CACHE_LINE);
    const uint8_t value[3] = {1, 2, 3};
    CHECK(spsc_write(&ring, value, 1) == 1);
    Snapshot before = snapshot(&ring);
    CHECK(!spsc_init(NULL, storage, sizeof(storage), 8, 1));
    CHECK(!spsc_init(&ring, NULL, sizeof(storage), 8, 1));
    CHECK(!spsc_init(&ring, storage, sizeof(storage), 0, 1));
    CHECK(!spsc_init(&ring, storage, sizeof(storage), 3, 1));
    CHECK(!spsc_init(&ring, storage, sizeof(storage), 8, 0));
    CHECK(!spsc_init(&ring, storage, 23, 8, 3));
    CHECK(!spsc_init(&ring, storage, SIZE_MAX, 8, SIZE_MAX));
    CHECK(!spsc_init(&ring, storage, SIZE_MAX, SIZE_MAX / 2 + 1, 1));
    CHECK(!spsc_init(&ring, (void *)(uintptr_t)(UINTPTR_MAX - 7), 32, 8, 2));
    unchanged(&ring, before);
    CHECK(memcmp(storage + 1, value, sizeof(value)) == 0);
    CHECK(!spsc_is_lock_free(NULL));
    CHECK(spsc_readable(NULL) == 0 && spsc_writable(NULL) == 0);
    CHECK(spsc_read(NULL, storage, 1) == 0);
    CHECK(spsc_write(NULL, storage, 1) == 0);
}

static void test_transfers(void)
{
    SpscRing ring;
    uint32_t storage[8], input[12], output[12];
    for (size_t i = 0; i < 12; ++i) {
        input[i] = (uint32_t)i;
        output[i] = UINT32_MAX;
    }
    CHECK(spsc_init(&ring, storage, sizeof(storage), 8, sizeof(uint32_t)));
    CHECK(spsc_readable(&ring) == 0 && spsc_writable(&ring) == 8);
    Snapshot before = snapshot(&ring);
    CHECK(spsc_read(&ring, output, 8) == 0);
    CHECK(output[0] == UINT32_MAX);
    CHECK(spsc_write(&ring, NULL, 1) == 0 && spsc_read(&ring, NULL, 1) == 0);
    CHECK(spsc_write(&ring, input, 0) == 0 && spsc_read(&ring, output, 0) == 0);
    unchanged(&ring, before);
    CHECK(spsc_write(&ring, input, 6) == 6);
    CHECK(spsc_readable(&ring) == 6 && spsc_writable(&ring) == 2);
    CHECK(spsc_read(&ring, output, 5) == 5);
    CHECK(memcmp(output, input, 5 * sizeof(uint32_t)) == 0);
    CHECK(output[5] == UINT32_MAX);
    /* Two copies on write (positions 6,7,0..4); no overwrite of element 5. */
    CHECK(spsc_write(&ring, input + 6, 6) == 6);
    CHECK(spsc_write(&ring, input, 2) == 1);
    CHECK(spsc_writable(&ring) == 0 && spsc_readable(&ring) == 8);
    before = snapshot(&ring);
    CHECK(spsc_write(&ring, input, SIZE_MAX) == 0);
    unchanged(&ring, before);
    CHECK(spsc_read(&ring, output, SIZE_MAX) == 8);
    CHECK(memcmp(output, input + 5, 7 * sizeof(uint32_t)) == 0);
    CHECK(output[7] == input[0] && output[8] == UINT32_MAX);
    CHECK(spsc_readable(&ring) == 0 && spsc_writable(&ring) == 8);
    CHECK(spsc_write(&ring, input, SIZE_MAX) == 8);
    CHECK(spsc_read(&ring, output, 12) == 8);
    CHECK(memcmp(output, input, sizeof(storage)) == 0);

    CHECK(spsc_init(&ring, storage, sizeof(storage), 1, sizeof(uint32_t)));
    for (size_t i = 0; i < 32; ++i) {
        CHECK(spsc_write(&ring, input, 2) == 1);
        CHECK(spsc_write(&ring, input + 1, 1) == 0);
        CHECK(spsc_read(&ring, output, 2) == 1 && output[0] == input[0]);
    }
}

static void seed_quiescent(SpscRing *ring, uint64_t cursor)
{
    /* Fixture only: neither endpoint is running. No production cursor setter. */
    atomic_store(&ring->read_count, cursor);
    atomic_store(&ring->write_count, cursor);
}

static void test_rollover(void)
{
    SpscRing ring;
    uint64_t storage[8], input[8], output[8];
    CHECK(spsc_init(&ring, storage, sizeof(storage), 8, sizeof(uint64_t)));
    seed_quiescent(&ring, UINT64_MAX - 3);
    for (size_t round = 0; round < 32; ++round) {
        for (size_t i = 0; i < 8; ++i) {
            input[i] = round * 8 + i;
        }
        CHECK(spsc_write(&ring, input, 8) == 8);
        CHECK(spsc_readable(&ring) == 8 && spsc_writable(&ring) == 0);
        CHECK(spsc_read(&ring, output, 3) == 3);
        CHECK(spsc_read(&ring, output + 3, 5) == 5);
        CHECK(memcmp(input, output, sizeof(input)) == 0);
    }
    CHECK(atomic_load(&ring.write_count) == 252);
    CHECK(atomic_load(&ring.read_count) == 252);
}

static uint8_t pcm_byte(size_t frame, size_t byte)
{
    return (uint8_t)(frame * 131 + byte * 17);
}

static void test_pcm(size_t rate, size_t stride)
{
    SpscRing ring;
    uint8_t storage[1024 * 12 + 2], input[480 * 12], output[480 * 12];
    size_t sent = 0, received = 0;
    size_t total = rate * 2;
    size_t period = rate / 100; /* 441 or 480 whole frames per 10 ms. */
    memset(storage, 0xa5, sizeof(storage));
    CHECK(spsc_init(&ring, storage + 1, 1024 * stride, 1024, stride));
    while (received < total) {
        size_t requested = total - sent < period ? total - sent : period;
        for (size_t i = 0; i < requested; ++i) {
            for (size_t b = 0; b < stride; ++b) {
                input[i * stride + b] = pcm_byte(sent + i, b);
            }
        }
        sent += spsc_write(&ring, input, requested);
        memset(output, 0xfe, sizeof(output));
        size_t count = spsc_read(&ring, output, period / 3 + 1);
        for (size_t i = 0; i < count; ++i) {
            for (size_t b = 0; b < stride; ++b) {
                CHECK(output[i * stride + b] == pcm_byte(received + i, b));
            }
        }
        CHECK(output[count * stride] == 0xfe);
        received += count;
    }
    CHECK(sent == total && spsc_readable(&ring) == 0);
    CHECK(storage[0] == 0xa5 && storage[1024 * stride + 1] == 0xa5);
}

static void test_pcm_budget(void)
{
    static uint8_t storage[SPSC_PCM_DEFAULT_BYTES + 64];
    SpscRing ring;
    CHECK(SPSC_PCM_DEFAULT_BYTES == 256 * 1024);
    CHECK(spsc_init_pcm(&ring, storage, sizeof(storage), 8));
    CHECK(ring.capacity == 32768 && ring.capacity * ring.stride == 256 * 1024);
    CHECK(spsc_init_pcm(&ring, storage, sizeof(storage), 6));
    CHECK(ring.capacity == 32768 && ring.capacity * ring.stride == 192 * 1024);
    CHECK(spsc_init_pcm(&ring, storage, 127, 6));
    CHECK(ring.capacity == 16);
    Snapshot before = snapshot(&ring);
    CHECK(!spsc_init_pcm(&ring, storage, sizeof(storage), 0));
    CHECK(!spsc_init_pcm(&ring, storage, 5, 6));
    CHECK(!spsc_init_pcm(&ring, storage, SIZE_MAX, SIZE_MAX));
    unchanged(&ring, before);
}

typedef struct Command {
    uint64_t sequence;
    uint32_t handle, generation;
    uint64_t check;
} Command;

typedef struct Stress {
    SpscRing *ring;
    HANDLE start;
    uint64_t total;
    uint64_t produced, consumed; /* Each written by its owner, read after join. */
} Stress;

static Command command(uint64_t sequence)
{
    return (Command){sequence, (uint32_t)(sequence % 257),
        (uint32_t)(sequence / 257 + 1), ~sequence};
}

static DWORD WINAPI produce(void *context)
{
    Stress *stress = context;
    Command batch[17];
    uint64_t sequence = 0;
    CHECK(WaitForSingleObject(stress->start, 10000) == WAIT_OBJECT_0);
    while (sequence < stress->total) {
        size_t count = (size_t)(stress->total - sequence);
        if (count > 17) { count = 17; }
        for (size_t i = 0; i < count; ++i) { batch[i] = command(sequence + i); }
        HEAP_WATCH(true);
        CHECK(spsc_writable(stress->ring) <= stress->ring->capacity);
        size_t written = spsc_write(stress->ring, batch, count);
        HEAP_WATCH(false);
        sequence += written;
        if (written == 0) { SwitchToThread(); }
    }
    stress->produced = sequence;
    return 0;
}

static DWORD WINAPI consume(void *context)
{
    Stress *stress = context;
    Command batch[31];
    uint64_t sequence = 0;
    CHECK(WaitForSingleObject(stress->start, 10000) == WAIT_OBJECT_0);
    while (sequence < stress->total) {
        HEAP_WATCH(true);
        CHECK(spsc_readable(stress->ring) <= stress->ring->capacity);
        size_t count = spsc_read(stress->ring, batch, 31);
        HEAP_WATCH(false);
        for (size_t i = 0; i < count; ++i) {
            Command expected = command(sequence + i);
            CHECK(batch[i].sequence == expected.sequence && batch[i].check == expected.check);
            CHECK(batch[i].handle == expected.handle && batch[i].generation == expected.generation);
        }
        sequence += count;
        if (count == 0) { SwitchToThread(); }
    }
    stress->consumed = sequence;
    return 0;
}

static void test_threaded(uint64_t initial_cursor)
{
    SpscRing ring;
    Command storage[1024];
    HANDLE threads[2];
    size_t open_handles = 0;
    HEAP_WATCH(true);
    CHECK(spsc_init(&ring, storage, sizeof(storage), 1024, sizeof(Command)));
    seed_quiescent(&ring, initial_cursor);
    HEAP_WATCH(false);
    Stress stress = {.ring = &ring, .total = 3000000};
    stress.start = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(stress.start != NULL);
    ++open_handles;
    threads[0] = CreateThread(NULL, 0, produce, &stress, 0, NULL);
    CHECK(threads[0] != NULL);
    ++open_handles;
    threads[1] = CreateThread(NULL, 0, consume, &stress, 0, NULL);
    CHECK(threads[1] != NULL);
    ++open_handles;
    CHECK(SetEvent(stress.start));
    CHECK(WaitForMultipleObjects(2, threads, TRUE, 40000) == WAIT_OBJECT_0);
    for (size_t i = 0; i < 2; ++i) {
        DWORD result;
        CHECK(GetExitCodeThread(threads[i], &result) && result == 0);
        CHECK(CloseHandle(threads[i]));
        --open_handles;
    }
    CHECK(CloseHandle(stress.start));
    --open_handles;
    CHECK(open_handles == 0);
    CHECK(stress.produced == stress.total && stress.consumed == stress.total);
    CHECK(spsc_readable(&ring) == 0 && spsc_writable(&ring) == ring.capacity);
    CHECK(atomic_load(&ring.read_count) == initial_cursor + stress.total);
    CHECK(atomic_load(&ring.write_count) == initial_cursor + stress.total);
    /* Joined endpoints acknowledge quiescence: reinit may now discard cursors. */
    HEAP_WATCH(true);
    CHECK(spsc_init(&ring, storage, sizeof(storage), 1024, sizeof(Command)));
    CHECK(spsc_readable(&ring) == 0 && atomic_load(&ring.write_count) == 0);
    HEAP_WATCH(false);
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(count_heap);
#endif
    HEAP_WATCH(true);
    test_init();
    test_transfers();
    test_rollover();
    test_pcm(44100, 4);
    test_pcm(44100, 8);
    test_pcm(48000, 6);
    test_pcm(48000, 12);
    test_pcm_budget();
    HEAP_WATCH(false);
    test_threaded(0);
    test_threaded(UINT64_MAX - 1023);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(previous);
    CHECK(atomic_load(&heap_calls) == 0);
#endif
    puts("SPSC boundaries, PCM frames and 6 million ordered concurrent records passed");
    return 0;
}
