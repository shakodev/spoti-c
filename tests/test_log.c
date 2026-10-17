#include "core/log.h"
#include "core/worker.h"
#include "support/fixture_clock.h"
#include "support/test_check.h"
#include <string.h>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
static _Thread_local bool watch_heap;
static _Atomic size_t heap_calls;
static int __cdecl count_heap(int type, void *data, size_t size, int block,
    long request, const unsigned char *file, int line)
{
    (void)type; (void)data; (void)size; (void)block; (void)request; (void)file; (void)line;
    if (watch_heap) { atomic_fetch_add(&heap_calls, 1); }
    return 1;
}
#define WATCH(value) (watch_heap = (value))
#else
#define WATCH(value) ((void)(value))
#endif

typedef struct Flood { Log *log; size_t iterations; } Flood;
static DWORD WINAPI flood(void *context)
{
    Flood *work = context;
    LogNumber fields[4] = {{.field = LOG_BYTES, .value = 19}};
    WATCH(true);
    for (size_t i = 0; i < work->iterations; ++i) {
        CHECK(log_emit(work->log, 1, 200, fields) == LOG_DROPPED);
    }
    WATCH(false);
    return 0;
}

typedef struct Capture {
    Log *log;
    char text[16384];
    size_t bytes, consumed;
    DWORD producer;
    CoreResult result;
} Capture;

static bool sink(void *context, const char *line, size_t bytes)
{
    Capture *capture = context;
    CHECK(GetCurrentThreadId() != capture->producer);
    CHECK(capture->bytes + bytes < sizeof(capture->text));
    memcpy(capture->text + capture->bytes, line, bytes);
    capture->bytes += bytes;
    capture->text[capture->bytes] = '\0';
    return true;
}
static void flush_job(const Event *event, const CancelToken *cancel, void *context)
{
    Capture *capture = context;
    (void)event; (void)cancel;
    capture->result = log_flush(capture->log, SIZE_MAX, sink, capture, &capture->consumed);
}
static void complete(const Event *event, WorkerOutcome outcome, void *context)
{
    (void)event; (void)context;
    CHECK(outcome == WORKER_DONE);
}
static bool fail_sink(void *context, const char *line, size_t bytes)
{
    (void)context; (void)line; (void)bytes; return false;
}
static void flush_on_worker(Worker *worker, Capture *capture)
{
    WorkerJob job = {.run = flush_job, .complete = complete, .context = capture,
        .deadline = CORE_CLOCK_NEVER};
    uint64_t ticket;
    CHECK(worker_submit(worker, &job, &ticket) == CORE_OK);
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    CHECK(capture->result == CORE_OK);
}

static void test_log(void)
{
    _Alignas(64) uint8_t memory[MEMORY_BUDGET_CORE];
    Metrics metrics;
    MetricsArena binding = {0};
    Arena core;
    FixtureClock clock;
    Log *log = NULL;
    Worker *worker;
    HANDLE threads[4];
    LogNumber fields[4] = {{.field = LOG_BYTES, .value = 19}};
    CHECK(metrics_init(&metrics) && arena_init(&core, memory, sizeof(memory)));
    CHECK(metrics_attach_arena(&metrics, &core, &binding));
    fixture_clock_init(&clock, 123);
    CoreClock source = fixture_clock_source(&clock);
    CHECK(log_create(&core, SIZE_MAX, source, &metrics, &log) == CORE_INVALID && log == NULL);
    CHECK(log_create(&core, 100000, source, &metrics, &log) == CORE_NO_MEMORY && core.used == 0);
    CHECK(metrics_snapshot(&metrics).handles == 0);
    CHECK(log_create(&core, 8, source, &metrics, &log) == CORE_OK);
    CHECK(worker_start(&core, 2, source, &worker) == CORE_OK);
    CHECK(WaitForSingleObject(worker_idle_handle(worker), 5000) == WAIT_OBJECT_0);
    size_t arena_before = core.used;
    WATCH(true);
    for (size_t i = 0; i < 8; ++i) { CHECK(log_emit(log, 1, 200, fields) == LOG_ACCEPTED); }
    WATCH(false);
    Flood work = {log, 25000};
    for (size_t i = 0; i < 4; ++i) {
        threads[i] = CreateThread(NULL, 0, flood, &work, 0, NULL); CHECK(threads[i] != NULL);
    }
    CHECK(WaitForMultipleObjects(4, threads, TRUE, 5000) == WAIT_OBJECT_0);
    for (size_t i = 0; i < 4; ++i) { CHECK(CloseHandle(threads[i])); }
    CHECK(log_dropped(log) == 100000 && core.used == arena_before);
    CHECK(metrics_snapshot(&metrics).log_dropped == 100000);
    CHECK(WaitForSingleObject(log_wait_handle(log), 0) == WAIT_OBJECT_0);
    Capture capture = {.log = log, .producer = GetCurrentThreadId()};
    flush_on_worker(worker, &capture);
    CHECK(capture.consumed == 8 && strstr(capture.text, "t=123 subsystem=1 code=200 bytes=19") != NULL);
    CHECK(WaitForSingleObject(log_wait_handle(log), 0) == WAIT_TIMEOUT);
    /* A producer must finish while another thread holds the drain lock. */
    AcquireSRWLockExclusive(&log->lock);
    work.iterations = 1;
    HANDLE contended = CreateThread(NULL, 0, flood, &work, 0, NULL);
    CHECK(contended != NULL && WaitForSingleObject(contended, 5000) == WAIT_OBJECT_0);
    CHECK(CloseHandle(contended));
    ReleaseSRWLockExclusive(&log->lock);
    CHECK(log_dropped(log) == 100001);
    static const char *const private_values[] = {
        "Bearer AUTH_SECRET", "CLIENT_TOKEN_SECRET", "123456_OAUTH_SECRET", "REUSABLE_SECRET",
        "BINARY_KEY_SECRET", "https://cdn.invalid/a?signature=SIGNED_SECRET",
        "wss://dealer.invalid/?access_token=DEALER_SECRET", "UNKNOWN_SECRET"};
    for (size_t i = 0; i < 8; ++i) {
        fields[0] = log_private((LogField)(LOG_AUTHORIZATION + i), private_values[i], strlen(private_values[i]));
        fields[1] = (LogNumber){.field = LOG_KEY, .value = UINT64_MAX};
        CHECK(log_emit(log, 2, 401, fields) == LOG_ACCEPTED);
        CHECK(log->records[(log->head + i) % log->capacity].fields[0].value == 0);
    }
    fields[0] = log_private(LOG_VALUE, private_values[0], strlen(private_values[0]));
    CHECK(fields[0].field == LOG_SECRET && fields[0].value == 0);
    capture.bytes = 0; capture.text[0] = '\0';
    flush_on_worker(worker, &capture);
    CHECK(strstr(capture.text, "authorization=[redacted]") != NULL);
    CHECK(strstr(capture.text, "client-token=[redacted]") != NULL);
    CHECK(strstr(capture.text, "oauth_code=[redacted]") != NULL);
    CHECK(strstr(capture.text, "credential=[redacted]") != NULL);
    CHECK(strstr(capture.text, "key=[redacted]") != NULL);
    CHECK(strstr(capture.text, "signed_url=[redacted]") != NULL);
    CHECK(strstr(capture.text, "dealer_url=[redacted]") != NULL);
    for (size_t i = 0; i < 8; ++i) { CHECK(strstr(capture.text, private_values[i]) == NULL); }
    CHECK(strstr(capture.text, "18446744073709551615") == NULL);
    size_t consumed;
    CHECK(log_emit(log, 3, 500, fields) == LOG_ACCEPTED);
    CHECK(log_flush(log, 1, fail_sink, NULL, &consumed) == CORE_OS_ERROR && consumed == 1);
    CHECK(metrics_snapshot(&metrics).log_write_failed == 1);
    CHECK(log_destroy(log) == CORE_INVALID);
    CHECK(worker_stop(worker) == CORE_OK && worker_join(worker, 5000) == CORE_OK);
    CHECK(log_close(log) == CORE_OK && log_emit(log, 1, 1, fields) == LOG_CLOSED);
    CHECK(log_destroy(log) == CORE_OK);
    CHECK(metrics_snapshot(&metrics).log_written == 16);
    arena_dispose(&core);
    CHECK(metrics_balanced(&metrics));
}

int main(void)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CRT_ALLOC_HOOK previous = _CrtSetAllocHook(count_heap);
#endif
    DWORD before, after;
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    test_log();
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after) && before == after);
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetAllocHook(previous); CHECK(atomic_load(&heap_calls) == 0);
#endif
    puts("Bounded nonblocking log flood, worker formatting, redaction and ownership passed");
    return 0;
}
