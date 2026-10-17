#include "log.h"
#include <inttypes.h>
#include <stdio.h>

CoreResult log_create(Arena *core, size_t capacity, CoreClock clock, Metrics *metrics, Log **output)
{
    ArenaMark mark;
    Log *log;
    HANDLE wake;
    if (output == NULL) { return CORE_INVALID; }
    *output = NULL;
    if (core == NULL || core->capacity > MEMORY_BUDGET_CORE || metrics == NULL || capacity == 0 ||
        capacity > (size_t)PTRDIFF_MAX / sizeof(LogRecord)) { return CORE_INVALID; }
    wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (wake == NULL) { return CORE_OS_ERROR; }
    mark = arena_mark(core);
    log = arena_alloc(core, sizeof(*log), _Alignof(Log));
    if (log != NULL) {
        *log = (Log){0};
        log->records = arena_alloc(core, capacity * sizeof(LogRecord), _Alignof(LogRecord));
    }
    if (log == NULL || log->records == NULL) {
        CloseHandle(wake); (void)arena_rewind(core, mark); return CORE_NO_MEMORY;
    }
    InitializeSRWLock(&log->lock);
    atomic_init(&log->dropped, 0);
    atomic_init(&log->closed, false);
    if (!atomic_is_lock_free(&log->dropped) || !atomic_is_lock_free(&log->closed)) {
        CloseHandle(wake); (void)arena_rewind(core, mark); return CORE_INVALID;
    }
    log->capacity = capacity; log->clock = clock; log->metrics = metrics; log->wake = wake;
    if (!metrics_handle_acquire(metrics, &log->wake_metric, (uintptr_t)wake)) {
        CloseHandle(wake); (void)arena_rewind(core, mark); return CORE_EXHAUSTED;
    }
    *output = log;
    return CORE_OK;
}

LogNumber log_private(LogField field, const void *private_value, size_t bytes)
{
    (void)private_value; (void)bytes;
    if (field < LOG_AUTHORIZATION || field >= LOG_FIELD_COUNT) { field = LOG_SECRET; }
    return (LogNumber){.field = field, .value = 0};
}

static LogEmit drop(Log *log)
{
    atomic_fetch_add_explicit(&log->dropped, 1, memory_order_relaxed);
    metrics_count(log->metrics, METRIC_LOG_DROP, 1);
    return LOG_DROPPED;
}

LogEmit log_emit(Log *log, uint32_t subsystem, uint32_t code, const LogNumber fields[4])
{
    LogRecord record = {0};
    if (log == NULL || fields == NULL) { return LOG_INVALID; }
    for (size_t i = 0; i < 4; ++i) {
        if ((unsigned)fields[i].field >= LOG_FIELD_COUNT) { return LOG_INVALID; }
        record.fields[i].field = fields[i].field;
        record.fields[i].value = fields[i].field >= LOG_AUTHORIZATION ? 0 : fields[i].value;
    }
    if (atomic_load_explicit(&log->closed, memory_order_acquire)) { return LOG_CLOSED; }
    if (!TryAcquireSRWLockExclusive(&log->lock)) { return drop(log); }
    if (atomic_load(&log->closed)) {
        ReleaseSRWLockExclusive(&log->lock); return LOG_CLOSED;
    }
    if (log->count == log->capacity || !SetEvent(log->wake)) {
        ReleaseSRWLockExclusive(&log->lock); return drop(log);
    }
    record.timestamp = core_clock_now(&log->clock);
    record.subsystem = subsystem; record.code = code;
    log->records[(log->head + log->count) % log->capacity] = record;
    ++log->count;
    metrics_count(log->metrics, METRIC_SIGNAL, 1);
    ReleaseSRWLockExclusive(&log->lock);
    return LOG_ACCEPTED;
}

uint64_t log_dropped(const Log *log)
{
    return log == NULL ? 0 : atomic_load(&log->dropped);
}

HANDLE log_wait_handle(const Log *log)
{
    return log == NULL ? NULL : log->wake;
}

static bool format(const LogRecord *record, char *line, size_t capacity, size_t *bytes)
{
    static const char *const names[LOG_FIELD_COUNT] = {
        "value", "bytes", "status", "request_id", "authorization", "client-token",
        "oauth_code", "credential", "key", "signed_url", "dealer_url", "secret"};
    int written = snprintf(line, capacity, "t=%" PRIu64 " subsystem=%" PRIu32 " code=%" PRIu32,
        record->timestamp, record->subsystem, record->code);
    if (written < 0 || (size_t)written >= capacity) { return false; }
    *bytes = (size_t)written;
    for (size_t i = 0; i < 4; ++i) {
        LogField field = record->fields[i].field;
        if ((unsigned)field >= LOG_FIELD_COUNT) { return false; }
        if (field >= LOG_AUTHORIZATION) {
            written = snprintf(line + *bytes, capacity - *bytes, " %s=[redacted]", names[field]);
        } else {
            written = snprintf(line + *bytes, capacity - *bytes, " %s=%" PRIu64,
                names[field], record->fields[i].value);
        }
        if (written < 0 || (size_t)written >= capacity - *bytes) { return false; }
        *bytes += (size_t)written;
    }
    if (*bytes + 1 >= capacity) { return false; }
    line[(*bytes)++] = '\n'; line[*bytes] = '\0';
    return true;
}

CoreResult log_flush(Log *log, size_t limit, LogSink sink, void *context, size_t *consumed)
{
    if (consumed == NULL) { return CORE_INVALID; }
    *consumed = 0;
    if (log == NULL || sink == NULL) { return CORE_INVALID; }
    if (limit > log->capacity) { limit = log->capacity; }
    while (*consumed < limit) {
        LogRecord record;
        char line[384];
        size_t bytes;
        AcquireSRWLockExclusive(&log->lock);
        if (log->count == 0) {
            bool ok = atomic_load(&log->closed) || ResetEvent(log->wake);
            ReleaseSRWLockExclusive(&log->lock);
            return ok ? CORE_OK : CORE_OS_ERROR;
        }
        record = log->records[log->head];
        log->head = (log->head + 1) % log->capacity;
        --log->count;
        bool ok = log->count != 0 || atomic_load(&log->closed) || ResetEvent(log->wake);
        ReleaseSRWLockExclusive(&log->lock);
        ++*consumed;
        if (!format(&record, line, sizeof(line), &bytes) || !sink(context, line, bytes)) {
            metrics_count(log->metrics, METRIC_LOG_WRITE_FAILED, 1); return CORE_OS_ERROR;
        }
        metrics_count(log->metrics, METRIC_LOG_WRITTEN, 1);
        if (!ok) { return CORE_OS_ERROR; }
    }
    return CORE_OK;
}

CoreResult log_close(Log *log)
{
    bool ok;
    if (log == NULL) { return CORE_INVALID; }
    AcquireSRWLockExclusive(&log->lock);
    atomic_store_explicit(&log->closed, true, memory_order_release);
    ok = SetEvent(log->wake) != 0;
    ReleaseSRWLockExclusive(&log->lock);
    return ok ? CORE_OK : CORE_OS_ERROR;
}

CoreResult log_destroy(Log *log)
{
    if (log == NULL || log->wake == NULL || !atomic_load(&log->closed) || log->count != 0) {
        return CORE_INVALID;
    }
    if (!CloseHandle(log->wake)) { return CORE_OS_ERROR; }
    log->wake = NULL;
    return metrics_handle_release(&log->wake_metric) ? CORE_OK : CORE_OS_ERROR;
}
