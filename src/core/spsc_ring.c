#include "spsc_ring.h"
#include <string.h>

bool spsc_is_lock_free(const SpscRing *ring)
{
    return ring != NULL && atomic_is_lock_free(&ring->read_count) &&
           atomic_is_lock_free(&ring->write_count);
}

bool spsc_init(SpscRing *ring, void *storage, size_t storage_bytes,
               size_t capacity, size_t stride)
{
    size_t bytes;
    if (ring == NULL || storage == NULL || capacity == 0 || stride == 0 ||
        (capacity & (capacity - 1)) != 0 || capacity > UINT64_MAX / 2 ||
        capacity > (size_t)PTRDIFF_MAX / stride || !spsc_is_lock_free(ring)) {
        return false;
    }
    bytes = capacity * stride;
    if (bytes > storage_bytes || bytes > UINTPTR_MAX - (uintptr_t)storage) {
        return false;
    }
    ring->storage = storage;
    ring->capacity = capacity;
    ring->stride = stride;
    atomic_init(&ring->read_count, 0);
    atomic_init(&ring->write_count, 0);
    return true;
}

bool spsc_init_pcm(SpscRing *ring, void *storage, size_t storage_bytes,
                   size_t frame_stride)
{
    size_t frames;
    size_t capacity = 1;
    if (frame_stride == 0) {
        return false;
    }
    if (storage_bytes > SPSC_PCM_DEFAULT_BYTES) {
        storage_bytes = SPSC_PCM_DEFAULT_BYTES;
    }
    frames = storage_bytes / frame_stride;
    if (frames == 0) {
        return false;
    }
    while (capacity <= frames / 2) {
        capacity *= 2;
    }
    return spsc_init(ring, storage, storage_bytes, capacity, frame_stride);
}

size_t spsc_readable(const SpscRing *ring)
{
    uint64_t read;
    uint64_t write;
    if (ring == NULL) {
        return 0;
    }
    read = atomic_load_explicit(&ring->read_count, memory_order_relaxed);
    write = atomic_load_explicit(&ring->write_count, memory_order_acquire);
    return (size_t)(write - read);
}

size_t spsc_writable(const SpscRing *ring)
{
    uint64_t read;
    uint64_t write;
    if (ring == NULL) {
        return 0;
    }
    write = atomic_load_explicit(&ring->write_count, memory_order_relaxed);
    read = atomic_load_explicit(&ring->read_count, memory_order_acquire);
    return ring->capacity - (size_t)(write - read);
}

size_t spsc_write(SpscRing *ring, const void *source, size_t elements)
{
    uint64_t write;
    uint64_t read;
    size_t available;
    size_t offset;
    size_t first;
    if (ring == NULL || source == NULL || elements == 0) {
        return 0;
    }
    write = atomic_load_explicit(&ring->write_count, memory_order_relaxed);
    /* Acquire pairs with consumer release: its reads finish before slot reuse. */
    read = atomic_load_explicit(&ring->read_count, memory_order_acquire);
    available = ring->capacity - (size_t)(write - read);
    if (elements > available) {
        elements = available;
    }
    if (elements == 0) {
        return 0;
    }
    offset = (size_t)write & (ring->capacity - 1);
    first = ring->capacity - offset;
    if (first > elements) {
        first = elements;
    }
    memcpy(ring->storage + offset * ring->stride, source, first * ring->stride);
    if (elements > first) {
        memcpy(ring->storage, (const uint8_t *)source + first * ring->stride,
               (elements - first) * ring->stride);
    }
    /* Publish only complete elements, after both copies finish. */
    atomic_store_explicit(&ring->write_count, write + elements, memory_order_release);
    return elements;
}

size_t spsc_read(SpscRing *ring, void *destination, size_t elements)
{
    uint64_t read;
    uint64_t write;
    size_t available;
    size_t offset;
    size_t first;
    if (ring == NULL || destination == NULL || elements == 0) {
        return 0;
    }
    read = atomic_load_explicit(&ring->read_count, memory_order_relaxed);
    /* Acquire observes producer copies before accessing published elements. */
    write = atomic_load_explicit(&ring->write_count, memory_order_acquire);
    available = (size_t)(write - read);
    if (elements > available) {
        elements = available;
    }
    if (elements == 0) {
        return 0;
    }
    offset = (size_t)read & (ring->capacity - 1);
    first = ring->capacity - offset;
    if (first > elements) {
        first = elements;
    }
    memcpy(destination, ring->storage + offset * ring->stride, first * ring->stride);
    if (elements > first) {
        memcpy((uint8_t *)destination + first * ring->stride, ring->storage,
               (elements - first) * ring->stride);
    }
    atomic_store_explicit(&ring->read_count, read + elements, memory_order_release);
    return elements;
}
