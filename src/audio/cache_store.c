#include "cache_internal.h"

static bool same(const CacheEntry *entry, const CacheRecord *record)
{
    return entry->valid && entry->offset == record->offset && memcmp(entry->file_id, record->file_id, 20) == 0;
}
CacheResult cache_disk_get(CacheJob *j, const CancelToken *cancel)
{
    AudioCache *c = j->owner;
    for (size_t i = 0; i < c->stats.capacity; ++i) {
        CacheEntry *e = &c->entries[i]; if (!same(e, &j->record)) { continue; }
        if (j->record.total != 0 && j->record.total != e->total) { return CACHE_CORRUPT; }
        uint8_t header[64]; CacheEntry decoded;
        CacheResult result = cache_io(c, false, 64 + i * (uint64_t)CACHE_RECORD, header, 64, cancel);
        if (result == CACHE_OK && (!cache_decode(header, &decoded) || !same(&decoded, &j->record) ||
            decoded.bytes != e->bytes || decoded.total != e->total || decoded.checksum != e->checksum)) { result = CACHE_CORRUPT; }
        if (result == CACHE_OK) { result = cache_io(c, false, 128 + i * (uint64_t)CACHE_RECORD, j->data, e->bytes, cancel); }
        if (result == CACHE_OK && cache_checksum(j->data, e->bytes) != e->checksum) { result = CACHE_CORRUPT; }
        if (result == CACHE_CORRUPT) {
            e->valid = false;
            AcquireSRWLockExclusive(&c->lock); --c->stats.entries; ++c->stats.errors; ReleaseSRWLockExclusive(&c->lock);
        }
        if (result != CACHE_OK) { return result; }
        if (c->stamp != UINT64_MAX) { e->stamp = ++c->stamp; }
        j->record.total = e->total; j->record.bytes = e->bytes;
        AcquireSRWLockExclusive(&c->lock); ++c->stats.hits; ReleaseSRWLockExclusive(&c->lock); return CACHE_OK;
    }
    AcquireSRWLockExclusive(&c->lock); ++c->stats.misses; ReleaseSRWLockExclusive(&c->lock); return CACHE_MISS;
}
CacheResult cache_disk_put(CacheJob *j, const CancelToken *cancel)
{
    AudioCache *c = j->owner; size_t selected = c->stats.capacity;
    for (size_t i = 0; i < c->stats.capacity; ++i) {
        CacheEntry *e = &c->entries[i];
        if (e->valid && memcmp(e->file_id, j->record.file_id, 20) == 0 && e->total != j->record.total) { return CACHE_CORRUPT; }
        if (same(e, &j->record)) {
            if (e->bytes != j->record.bytes || e->checksum != cache_checksum(j->data, j->record.bytes)) { return CACHE_CORRUPT; }
            return CACHE_OK;
        }
    }
    if (c->stamp == UINT64_MAX) { return CACHE_EXHAUSTED; }
    for (size_t i = 0; i < c->stats.capacity; ++i) { if (!c->entries[i].valid) { selected = i; break; } }
    if (selected == c->stats.capacity) {
        selected = 0;
        for (size_t i = 1; i < c->stats.capacity; ++i) { if (c->entries[i].stamp < c->entries[selected].stamp) { selected = i; } }
    }
    CacheEntry *e = &c->entries[selected]; bool evicted = e->valid;
    uint8_t header[64] = {0}; uint64_t base = 64 + selected * (uint64_t)CACHE_RECORD;
    /* Invalidate before changing ciphertext. Publish complete metadata only
     * after data/extent are durable. An interrupted slot is a cache hole. */
    CacheResult result = cache_io(c, true, base, header, 64, cancel);
    if (result != CACHE_OK) { return result; }
    e->valid = false;
    AcquireSRWLockExclusive(&c->lock);
    if (evicted) { --c->stats.entries; ++c->stats.evictions; } ReleaseSRWLockExclusive(&c->lock);
    if (!FlushFileBuffers(c->file)) { return cache_disk_error(c, GetLastError()); }
    result = cache_io(c, true, base + 64, j->data, j->record.bytes, cancel);
    if (result != CACHE_OK) { return result; }
    size_t extent = selected >= c->extent ? selected + 1 : c->extent;
    LARGE_INTEGER end; end.QuadPart = (LONGLONG)(64 + extent * (uint64_t)CACHE_RECORD);
    if ((uint64_t)end.QuadPart > c->stats.quota) { return CACHE_QUOTA; }
    if (!SetFilePointerEx(c->file, end, NULL, FILE_BEGIN) || !SetEndOfFile(c->file) || !FlushFileBuffers(c->file)) {
        return cache_disk_error(c, GetLastError());
    }
    CacheEntry fresh = {0}; memcpy(fresh.file_id, j->record.file_id, 20); fresh.bytes = (uint32_t)j->record.bytes;
    fresh.offset = j->record.offset; fresh.total = j->record.total; fresh.checksum = cache_checksum(j->data, j->record.bytes);
    fresh.stamp = ++c->stamp; fresh.valid = true; cache_encode(header, &fresh);
    result = cache_io(c, true, base, header, 64, cancel);
    if (result != CACHE_OK) { return result; }
    if (!FlushFileBuffers(c->file)) { return cache_disk_error(c, GetLastError()); }
    *e = fresh; c->extent = extent;
    AcquireSRWLockExclusive(&c->lock); ++c->stats.entries; c->stats.file_bytes = (uint64_t)end.QuadPart; ReleaseSRWLockExclusive(&c->lock);
    return CACHE_OK;
}
