#include "cache_internal.h"
#include <wchar.h>

CacheResult cache_disk_error(AudioCache *c, DWORD error)
{
    AcquireSRWLockExclusive(&c->lock); ++c->stats.errors; c->stats.os_error = error; ReleaseSRWLockExclusive(&c->lock);
    return CACHE_IO_ERROR;
}
CacheResult cache_io(AudioCache *c, bool write, uint64_t offset, void *data, size_t bytes, const CancelToken *cancel)
{
    if (worker_cancelled(cancel)) { return CACHE_CANCELLED; }
    if (bytes > AUDIO_CACHE_BLOCK || offset > INT64_MAX - bytes) { return CACHE_INVALID; }
    if (write && c->config.fixture_write_limit != 0 &&
        (c->written > c->config.fixture_write_limit || bytes > c->config.fixture_write_limit - c->written)) {
        return cache_disk_error(c, ERROR_DISK_FULL);
    }
    OVERLAPPED ov = {0}; ov.Offset = (DWORD)offset; ov.OffsetHigh = (DWORD)(offset >> 32); ov.hEvent = c->io;
    if (!ResetEvent(c->io)) { return cache_disk_error(c, GetLastError()); }
    DWORD transferred = 0;
    BOOL immediate = write ? WriteFile(c->file, data, (DWORD)bytes, &transferred, &ov) : ReadFile(c->file, data, (DWORD)bytes, &transferred, &ov);
    if (!immediate) {
        DWORD error = GetLastError(); if (error != ERROR_IO_PENDING) { return cache_disk_error(c, error); }
        HANDLE handles[] = {c->io, worker_cancel_handle(cancel)};
        DWORD timeout = worker_cancel_wait(cancel); if (timeout > c->config.io_timeout_ms) { timeout = c->config.io_timeout_ms; }
        DWORD waited = WaitForMultipleObjects(2, handles, FALSE, timeout);
        if (waited != WAIT_OBJECT_0) {
            (void)CancelIoEx(c->file, &ov);
            /* Cancellation is asynchronous: acknowledge retirement before the
             * worker returns and releases its borrowed block/OVERLAPPED. */
            (void)GetOverlappedResult(c->file, &ov, &transferred, TRUE);
            return waited == WAIT_TIMEOUT ? CACHE_TIMEOUT : CACHE_CANCELLED;
        }
        if (!GetOverlappedResult(c->file, &ov, &transferred, FALSE)) { return cache_disk_error(c, GetLastError()); }
    }
    if (transferred != bytes) { return write ? cache_disk_error(c, ERROR_DISK_FULL) : CACHE_CORRUPT; }
    if (write) { c->written += bytes; } return CACHE_OK;
}

CacheResult cache_disk_close(AudioCache *c)
{
    if (c->file != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(c->file)) { return cache_disk_error(c, GetLastError()); } c->file = INVALID_HANDLE_VALUE;
        if (c->file_metric.owner != NULL) { (void)metrics_handle_release(&c->file_metric); }
    }
    if (c->io != NULL) {
        if (!CloseHandle(c->io)) { return cache_disk_error(c, GetLastError()); } c->io = NULL;
        if (c->io_metric.owner != NULL) { (void)metrics_handle_release(&c->io_metric); }
    }
    return CACHE_OK;
}
static CacheResult open_file(AudioCache *c)
{
    if (!CreateDirectoryW(c->path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) { return cache_disk_error(c, GetLastError()); }
    if (wcscat_s(c->path, MAX_PATH, L"\\encrypted-v1.bin") != 0) { return CACHE_INVALID; }
    c->file = CreateFileW(c->path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (c->file == INVALID_HANDLE_VALUE) { return cache_disk_error(c, GetLastError()); }
    if (c->config.metrics != NULL && !metrics_handle_acquire(c->config.metrics, &c->file_metric, (uintptr_t)c->file)) { return CACHE_IO_ERROR; }
    c->io = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (c->io == NULL) { return cache_disk_error(c, GetLastError()); }
    if (c->config.metrics != NULL && !metrics_handle_acquire(c->config.metrics, &c->io_metric, (uintptr_t)c->io)) { return CACHE_IO_ERROR; }
    return CACHE_OK;
}
CacheResult cache_disk_open(AudioCache *c, const CancelToken *cancel)
{
    CacheResult result = open_file(c);
    if (result != CACHE_OK) { (void)cache_disk_close(c); return result; }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(c->file, &size) || size.QuadPart < 0) { result = cache_disk_error(c, GetLastError()); }
    else if ((uint64_t)size.QuadPart > c->stats.quota) { result = CACHE_QUOTA; }
    else {
        uint8_t h[64] = {0};
        if (size.QuadPart == 0) {
            memcpy(h, "SPTCAC01", 8); cache_set32(h + 8, 1); cache_set32(h + 12, AUDIO_CACHE_BLOCK);
            cache_set32(h + 16, (uint32_t)c->stats.capacity); cache_set64(h + 24, c->stats.quota);
            cache_set32(h + 32, cache_checksum(h, 64));
            result = cache_io(c, true, 0, h, sizeof(h), cancel);
            if (result == CACHE_OK && !FlushFileBuffers(c->file)) { result = cache_disk_error(c, GetLastError()); }
            size.QuadPart = 64;
        } else if (size.QuadPart < 64 || (size.QuadPart - 64) % CACHE_RECORD != 0) { result = CACHE_CORRUPT; }
        else {
            result = cache_io(c, false, 0, h, sizeof(h), cancel);
            uint32_t checksum = cache_u32(h + 32); cache_set32(h + 32, 0);
            if (result == CACHE_OK && (memcmp(h, "SPTCAC01", 8) != 0 || cache_u32(h + 8) != 1 ||
                cache_u32(h + 12) != AUDIO_CACHE_BLOCK)) { result = CACHE_VERSION; }
            if (result == CACHE_OK && (checksum != cache_checksum(h, 64) || cache_u32(h + 16) != c->stats.capacity ||
                cache_u64(h + 24) != c->stats.quota)) { result = CACHE_CORRUPT; }
        }
        size_t extent = (size_t)((size.QuadPart - 64) / CACHE_RECORD);
        if (result == CACHE_OK && extent > c->stats.capacity) { result = CACHE_QUOTA; }
        for (size_t i = 0; result == CACHE_OK && i < extent; ++i) {
            result = cache_io(c, false, 64 + i * (uint64_t)CACHE_RECORD, h, sizeof(h), cancel);
            if (result != CACHE_OK) { break; }
            CacheEntry entry;
            if (!cache_decode(h, &entry)) { continue; }
            bool duplicate = false;
            for (size_t j = 0; j < i; ++j) {
                CacheEntry *other = &c->entries[j];
                if (other->valid && memcmp(other->file_id, entry.file_id, 20) == 0 &&
                    (other->total != entry.total || other->offset == entry.offset)) { duplicate = true; break; }
            }
            if (duplicate) { continue; }
            c->entries[i] = entry;
            if (entry.stamp > c->stamp) { c->stamp = entry.stamp; }
            AcquireSRWLockExclusive(&c->lock); ++c->stats.entries; ReleaseSRWLockExclusive(&c->lock);
        }
        if (result == CACHE_OK) {
            c->extent = extent;
            AcquireSRWLockExclusive(&c->lock); c->stats.file_bytes = (uint64_t)size.QuadPart; ReleaseSRWLockExclusive(&c->lock);
        }
    }
    if (result != CACHE_OK) { (void)cache_disk_close(c); } return result;
}
