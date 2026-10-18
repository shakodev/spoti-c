#include "cache_internal.h"
#include <wchar.h>

static void run(const Event *event, const CancelToken *cancel, void *context)
{
    (void)event; CacheJob *j = context; AudioCache *c = j->owner;
    if (j->operation == CO_OPEN) { j->result = cache_disk_open(c, cancel); }
    else if (j->operation == CO_READ) { j->result = cache_disk_get(j, cancel); }
    else if (j->operation == CO_WRITE) { j->result = cache_disk_put(j, cancel); }
    else { j->result = cache_disk_close(c); }
}
static void complete(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; CacheJob *j = context; AudioCache *c = j->owner;
    AcquireSRWLockExclusive(&c->lock);
    if (outcome != WORKER_DONE && j->operation != CO_CLOSE) {
        j->result = outcome == WORKER_EXPIRED ? CACHE_TIMEOUT : CACHE_CANCELLED;
    }
    j->state = CJ_DONE; SetEvent(c->wake);
    ReleaseSRWLockExclusive(&c->lock); /* No cache access after publication. */
}
static CacheResult submit(AudioCache *c, enum CacheOperation operation, const CacheRecord *record,
    void *data, CacheCallback callback, void *context, uint64_t *ticket)
{
    CacheJob *j = NULL;
    AcquireSRWLockExclusive(&c->lock);
    for (size_t i = 0; i < AUDIO_CACHE_JOBS; ++i) { if (c->jobs[i].state == CJ_FREE) { j = &c->jobs[i]; break; } }
    if (j == NULL) { c->worker_blocked = false; ReleaseSRWLockExclusive(&c->lock); return CACHE_FULL; }
    *j = (CacheJob){.owner = c, .operation = operation, .state = CJ_ACTIVE, .data = data,
        .callback = callback, .context = context, .result = CACHE_CANCELLED};
    if (record != NULL) { j->record = *record; }
    WorkerJob job = {.run = run, .complete = complete, .context = j, .deadline = CORE_CLOCK_NEVER, .priority = WORKER_DISK};
    CoreResult result = worker_submit(c->config.worker, &job, &j->ticket);
    c->worker_blocked = result == CORE_FULL;
    if (result != CORE_OK) { memset(j, 0, sizeof(*j)); ReleaseSRWLockExclusive(&c->lock); return result == CORE_FULL ? CACHE_FULL : CACHE_CLOSED; }
    ++c->stats.jobs; if (ticket != NULL) { *ticket = j->ticket; }
    ReleaseSRWLockExclusive(&c->lock); return CACHE_OK;
}

CacheResult audio_cache_create(Arena *arena, const AudioCacheConfig *config, AudioCache **out)
{
    if (out == NULL) { return CACHE_INVALID; } *out = NULL;
    if (arena == NULL || config == NULL || config->worker == NULL || config->directory == NULL ||
        wcsnlen_s(config->directory, 241) > 240 || config->directory[0] == 0 ||
        config->entry_limit > AUDIO_CACHE_ENTRIES) { return CACHE_INVALID; }
    wchar_t resolved[MAX_PATH];
    DWORD length = GetFullPathNameW(config->directory, MAX_PATH, resolved, NULL);
    if (length == 0 || length > 240 || _wcsicmp(resolved, config->directory) != 0) { return CACHE_INVALID; }
    uint64_t quota = config->quota == 0 ? AUDIO_CACHE_QUOTA : config->quota;
    if (quota < CACHE_HEADER + CACHE_RECORD || quota > AUDIO_CACHE_QUOTA) { return CACHE_QUOTA; }
    ArenaMark mark = arena_mark(arena);
    AudioCache *c = arena_alloc(arena, sizeof(*c), _Alignof(AudioCache));
    if (c == NULL) { return CACHE_FULL; }
    memset(c, 0, sizeof(*c)); c->config = *config; c->file = INVALID_HANDLE_VALUE; c->status = CACHE_PENDING;
    c->config.directory = NULL; c->config.quota = quota;
    if (c->config.io_timeout_ms == 0) { c->config.io_timeout_ms = 3000; }
    size_t slots = (size_t)((quota - CACHE_HEADER) / CACHE_RECORD);
    size_t limit = config->entry_limit == 0 ? AUDIO_CACHE_ENTRIES : config->entry_limit;
    c->stats.capacity = slots < limit ? slots : limit; c->stats.quota = quota; c->stats.index_bytes = sizeof(c->entries);
    wcscpy_s(c->path, MAX_PATH, resolved); InitializeSRWLock(&c->lock);
    c->wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (c->wake == NULL) { (void)arena_rewind(arena, mark); return CACHE_IO_ERROR; }
    if (config->metrics != NULL && !metrics_handle_acquire(config->metrics, &c->wake_metric, (uintptr_t)c->wake)) {
        CloseHandle(c->wake); (void)arena_rewind(arena, mark); return CACHE_IO_ERROR;
    }
    CacheResult result = submit(c, CO_OPEN, NULL, NULL, NULL, NULL, NULL);
    if (result != CACHE_OK) {
        CloseHandle(c->wake); if (c->wake_metric.owner != NULL) { (void)metrics_handle_release(&c->wake_metric); }
        (void)arena_rewind(arena, mark); return result;
    }
    *out = c; return CACHE_OK;
}

CacheResult audio_cache_status(const AudioCache *c) { return c == NULL ? CACHE_INVALID : c->status; }
static CacheResult admission(AudioCache *c, const CacheRecord *record, const void *block, CacheCallback callback, uint64_t *ticket, bool write)
{
    if (c == NULL || record == NULL || block == NULL || callback == NULL || ticket == NULL ||
        record->offset % AUDIO_CACHE_BLOCK != 0 || (write && (record->total <= record->offset ||
        record->bytes == 0 || record->bytes > AUDIO_CACHE_BLOCK ||
        record->bytes != (record->total - record->offset < AUDIO_CACHE_BLOCK ? (size_t)(record->total - record->offset) : AUDIO_CACHE_BLOCK)))) { return CACHE_INVALID; }
    if (c->busy) { return CACHE_BAD_STATE; }
    if (c->closing) { return CACHE_CLOSED; }
    return c->status;
}
CacheResult audio_cache_get(AudioCache *c, const CacheRecord *record, void *block, CacheCallback callback, void *context, uint64_t *ticket)
{
    CacheResult result = admission(c, record, block, callback, ticket, false);
    return result == CACHE_OK ? submit(c, CO_READ, record, block, callback, context, ticket) : result;
}
CacheResult audio_cache_put(AudioCache *c, const CacheRecord *record, const void *data, CacheCallback callback, void *context, uint64_t *ticket)
{
    CacheResult result = admission(c, record, data, callback, ticket, true);
    return result == CACHE_OK ? submit(c, CO_WRITE, record, (void *)data, callback, context, ticket) : result;
}
CacheResult audio_cache_cancel(AudioCache *c, uint64_t ticket)
{
    if (c == NULL || ticket == 0) { return CACHE_INVALID; }
    if (c->busy) { return CACHE_BAD_STATE; }
    return worker_cancel(c->config.worker, ticket) ? CACHE_OK : CACHE_STALE;
}
CacheResult audio_cache_poll(AudioCache *c)
{
    if (c == NULL) { return CACHE_INVALID; }
    if (c->busy) { return CACHE_BAD_STATE; }
    c->busy = true;
    AcquireSRWLockExclusive(&c->lock); if (c->wake != NULL) { ResetEvent(c->wake); } ReleaseSRWLockExclusive(&c->lock);
    for (size_t i = 0; i < AUDIO_CACHE_JOBS; ++i) {
        CacheJob *j = &c->jobs[i];
        AcquireSRWLockExclusive(&c->lock);
        if (j->state != CJ_DONE) { ReleaseSRWLockExclusive(&c->lock); continue; }
        j->state = CJ_DELIVER; ReleaseSRWLockExclusive(&c->lock);
        if (j->operation == CO_OPEN) { c->status = j->result; }
        else if (j->operation == CO_CLOSE) { c->status = j->result == CACHE_OK ? CACHE_CLOSED : j->result; }
        else { j->callback(j->context, j->ticket, j->result, &j->record); }
        AcquireSRWLockExclusive(&c->lock); memset(j, 0, sizeof(*j)); --c->stats.jobs; ReleaseSRWLockExclusive(&c->lock);
    }
    c->busy = false; return CACHE_OK;
}
HANDLE audio_cache_wait_handle(const AudioCache *c) { return c == NULL ? NULL : c->wake; }
HANDLE audio_cache_retry_handle(const AudioCache *c)
{
    return c != NULL && c->wake != NULL && c->worker_blocked ? worker_idle_handle(c->config.worker) : NULL;
}
AudioCacheStats audio_cache_stats(AudioCache *c)
{
    AudioCacheStats stats = {0}; if (c == NULL) { return stats; }
    AcquireSRWLockShared(&c->lock); stats = c->stats; ReleaseSRWLockShared(&c->lock); return stats;
}
CacheResult audio_cache_shutdown(AudioCache *c)
{
    if (c == NULL) { return CACHE_INVALID; }
    if (c->busy) { return CACHE_BAD_STATE; }
    c->closing = true; (void)audio_cache_poll(c);
    AcquireSRWLockShared(&c->lock); size_t jobs = c->stats.jobs; ReleaseSRWLockShared(&c->lock);
    if (jobs != 0) {
        for (size_t i = 0; i < AUDIO_CACHE_JOBS; ++i) {
            AcquireSRWLockShared(&c->lock);
            bool cancel = c->jobs[i].state == CJ_ACTIVE && c->jobs[i].operation != CO_OPEN && c->jobs[i].operation != CO_CLOSE;
            uint64_t ticket = c->jobs[i].ticket; ReleaseSRWLockShared(&c->lock);
            if (cancel) { (void)worker_cancel(c->config.worker, ticket); }
        }
        return CACHE_PENDING;
    }
    if (!c->close_queued) {
        CacheResult result = submit(c, CO_CLOSE, NULL, NULL, NULL, NULL, NULL);
        if (result != CACHE_OK) { return result; } c->close_queued = true; return CACHE_PENDING;
    }
    if (c->wake != NULL) {
        if (!CloseHandle(c->wake)) { return CACHE_IO_ERROR; } c->wake = NULL;
        if (c->wake_metric.owner != NULL) { (void)metrics_handle_release(&c->wake_metric); }
    }
    return c->file == INVALID_HANDLE_VALUE && c->io == NULL ? CACHE_OK : CACHE_IO_ERROR;
}
