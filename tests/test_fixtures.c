#include "support/fixture_http.h"
#include "support/fixture_clock.h"
#include "support/test_check.h"
#include <string.h>

static const char request[] = "GET /fixture HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";

typedef struct ClientWork {
    FixtureTransport transport;
    HANDLE cancel;
    CoreResult result;
    char response[FIXTURE_HTTP_RESPONSE_MAX + 1];
    size_t received;
} ClientWork;

static DWORD WINAPI exchange(void *context)
{
    ClientWork *work = context;
    work->result = work->transport.exchange(&work->transport.endpoint, request, sizeof(request) - 1,
        work->cancel, work->response, FIXTURE_HTTP_RESPONSE_MAX, &work->received);
    return 0;
}

static void join_client(HANDLE thread)
{
    DWORD code;
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CHECK(GetExitCodeThread(thread, &code) && code == 0);
    CHECK(CloseHandle(thread));
}

static void test_connect(Metrics *metrics)
{
    FixtureHttp server = {0};
    static const char body[] = "{\"offline\":true}";
    CHECK(fixture_http_start(&server, metrics, 200, body, sizeof(body) - 1, false) == CORE_OK);
    CHECK(server.port != 0);
    ClientWork work = {.transport = fixture_http_transport(&server)};
    exchange(&work);
    CHECK(work.result == CORE_OK);
    CHECK(strstr(work.response, "HTTP/1.1 200 Fixture\r\n") == work.response);
    const char *payload = strstr(work.response, "\r\n\r\n");
    CHECK(payload != NULL && strcmp(payload + 4, body) == 0);
    CHECK(WaitForSingleObject(server.seen, 0) == WAIT_OBJECT_0);
    CHECK(strncmp(server.request, "GET /fixture HTTP/1.1\r\n", 23) == 0);
    CHECK(fixture_http_stop(&server) == CORE_OK && fixture_http_stop(&server) == CORE_CLOSED);
    CHECK(metrics_balanced(metrics) && !server.winsock_started);
}

static void test_cancel(Metrics *metrics, bool stop_server)
{
    FixtureHttp server = {0};
    MetricsHandle cancel_metric = {0};
    CHECK(fixture_http_start(&server, metrics, 429, "retry", 5, true) == CORE_OK);
    HANDLE cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(cancel != NULL && metrics_handle_acquire(metrics, &cancel_metric, (uintptr_t)cancel));
    ClientWork work = {.transport = fixture_http_transport(&server), .cancel = cancel};
    HANDLE thread = CreateThread(NULL, 0, exchange, &work, 0, NULL);
    CHECK(thread != NULL && WaitForSingleObject(server.seen, 5000) == WAIT_OBJECT_0);
    if (stop_server) { CHECK(fixture_http_stop(&server) == CORE_OK); }
    else { CHECK(SetEvent(cancel)); }
    join_client(thread);
    CHECK(work.result == (stop_server ? CORE_OS_ERROR : CORE_CLOSED));
    CHECK(work.received == 0);
    if (!stop_server) { CHECK(fixture_http_stop(&server) == CORE_OK); }
    CHECK(CloseHandle(cancel) && metrics_handle_release(&cancel_metric));
    CHECK(metrics_balanced(metrics));
}

static void test_release_and_limits(Metrics *metrics)
{
    FixtureHttp server = {0};
    CHECK(fixture_http_start(&server, metrics, 201, "created", 7, true) == CORE_OK);
    ClientWork work = {.transport = fixture_http_transport(&server)};
    HANDLE thread = CreateThread(NULL, 0, exchange, &work, 0, NULL);
    CHECK(thread != NULL && WaitForSingleObject(server.seen, 5000) == WAIT_OBJECT_0);
    CHECK(SetEvent(server.release));
    join_client(thread);
    CHECK(work.result == CORE_OK && strstr(work.response, "created") != NULL);
    CHECK(fixture_http_stop(&server) == CORE_OK && metrics_balanced(metrics));
    CHECK(fixture_http_start(&server, metrics, 200, "bounded", 7, false) == CORE_OK);
    FixtureTransport transport = fixture_http_transport(&server);
    unsigned char output[9] = {0};
    output[8] = 0xa5;
    size_t received;
    CHECK(transport.exchange(&transport.endpoint, request, sizeof(request) - 1, NULL,
        output, 8, &received) == CORE_FULL && received == 8 && output[8] == 0xa5);
    CHECK(fixture_http_stop(&server) == CORE_OK && metrics_balanced(metrics));
    CHECK(fixture_http_start(&server, metrics, 200, NULL, 1, false) == CORE_INVALID);
    CHECK(fixture_http_start(&server, metrics, 200, output, FIXTURE_HTTP_BODY_MAX + 1, false) == CORE_INVALID);
    /* Repeatedly stop an idle listener without polling. */
    for (size_t i = 0; i < 8; ++i) {
        CHECK(fixture_http_start(&server, metrics, 200, NULL, 0, false) == CORE_OK);
        CHECK(fixture_http_stop(&server) == CORE_OK && metrics_balanced(metrics));
    }
}

static CoreResult fake_exchange(const FixtureEndpoint *endpoint, const void *bytes,
    size_t request_bytes, HANDLE cancel, void *response, size_t capacity, size_t *received)
{
    (void)endpoint; (void)bytes; (void)request_bytes; (void)cancel;
    if (capacity < 7) { return CORE_FULL; }
    memcpy(response, "offline", 7); *received = 7; return CORE_OK;
}

int main(void)
{
    Metrics metrics;
    FixtureClock clock;
    DWORD before, after;
    CHECK(metrics_init(&metrics));
    /* Initialize Winsock's process-wide provider state before the leak baseline.
     * The warmup itself must still release every explicitly tracked resource. */
    test_connect(&metrics);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &before));
    fixture_clock_init(&clock, 1000);
    CoreClock source = fixture_clock_source(&clock);
    uint64_t deadline = core_deadline_after(core_clock_now(&source), core_backoff_ms(100, 1000, 2));
    CHECK(fixture_clock_advance(&clock, 399) && !core_deadline_expired(core_clock_now(&source), deadline));
    CHECK(fixture_clock_advance(&clock, 1) && core_deadline_expired(core_clock_now(&source), deadline));
    char output[16] = {0};
    size_t received = 0;
    FixtureTransport fake = {.exchange = fake_exchange};
    CHECK(fake.exchange(&fake.endpoint, request, sizeof(request) - 1, NULL,
        output, sizeof(output), &received) == CORE_OK && received == 7);
    CHECK(strcmp(output, "offline") == 0);
    test_connect(&metrics); test_cancel(&metrics, false); test_cancel(&metrics, true);
    test_release_and_limits(&metrics);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &after));
    if (before != after) { fprintf(stderr, "handles before=%lu after=%lu\n", before, after); }
    /* Winsock may retire process-wide provider handles asynchronously. Require
     * no process handle growth; exact per-resource balance is checked separately
     * by the metrics leases after every fixture scenario, including warmup. */
    CHECK(after <= before);
    CHECK(metrics_balanced(&metrics));
    puts("Offline clock/transport injection and bounded loopback connect/cancel/stop passed");
    return 0;
}
