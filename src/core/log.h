#ifndef SPOTIC_CORE_LOG_H
#define SPOTIC_CORE_LOG_H

#include "clock.h"
#include "event_queue.h"
#include "metrics.h"

typedef enum LogField {
    LOG_VALUE, LOG_BYTES, LOG_STATUS, LOG_REQUEST_ID,
    LOG_AUTHORIZATION, LOG_CLIENT_TOKEN, LOG_OAUTH_CODE, LOG_CREDENTIAL,
    LOG_KEY, LOG_SIGNED_URL, LOG_DEALER_URL, LOG_SECRET, LOG_FIELD_COUNT
} LogField;

typedef struct LogNumber { uint64_t value; LogField field; } LogNumber;
typedef struct LogRecord {
    uint64_t timestamp;
    uint32_t subsystem, code;
    LogNumber fields[4];
} LogRecord;

typedef struct Log {
    SRWLOCK lock;
    HANDLE wake;
    MetricsHandle wake_metric;
    Metrics *metrics;
    CoreClock clock;
    LogRecord *records;
    size_t capacity, head, count;
    _Atomic uint64_t dropped;
    _Atomic bool closed;
} Log;

typedef enum LogEmit { LOG_ACCEPTED, LOG_DROPPED, LOG_CLOSED, LOG_INVALID } LogEmit;
typedef bool (*LogSink)(void *context, const char *line, size_t bytes);

CoreResult log_create(Arena *core, size_t capacity, CoreClock clock, Metrics *metrics, Log **output);
/* Private text/binary values are NEVER read, copied, hashed, or retained. Even
 * an incorrect/non-private tag fails closed to LOG_SECRET. All other log fields
 * accept numbers only; no arbitrary message, header, URL, or printf API exists. */
LogNumber log_private(LogField field, const void *private_value, size_t bytes);
/* Nonblocking try-lock: full OR contended drops one record and increments an
 * atomic counter. No allocation, formatting, I/O sink, or waiting for a lock.
 * NOT an audio callback API: audio updates metrics_count only. */
LogEmit log_emit(Log *log, uint32_t subsystem, uint32_t code, const LogNumber fields[4]);
uint64_t log_dropped(const Log *log);
HANDLE log_wait_handle(const Log *log);
/* One non-audio worker drains/formats at most min(limit, capacity) records.
 * Sink runs outside the queue lock; failed
 * writes are counted and return OS_ERROR, with the consumed record not retried.
 * Sink must not reenter flush. No formatting ever occurs on producer threads. */
CoreResult log_flush(Log *log, size_t limit, LogSink sink, void *context, size_t *consumed);
CoreResult log_close(Log *log);
/* Stop/join emitters, close, flush, then destroy; all users quiescent before
 * destroying/reclaiming the shared <=128 KiB core arena. */
CoreResult log_destroy(Log *log);

#endif
