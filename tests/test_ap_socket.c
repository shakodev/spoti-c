#include "protocol/ap_internal.h"
#include "support/ap_server.h"
#include "ap_test.h"
#include "ap_vectors.h"

static void wait_io(ApTransport *io)
{
    HANDLE handles[2]; size_t n = io->handles(io->context, handles, 2);
    CHECK(n > 0 && n <= 2 && WaitForMultipleObjects((DWORD)n, handles, FALSE, 5000) == WAIT_OBJECT_0);
}

static void native_transport(void)
{
    Metrics metrics; CHECK(metrics_init(&metrics));
    TestApServer server;
    static const uint8_t response[] = "partial AP";
    test_ap_server_start(&server, &metrics, 6, response, sizeof(response) - 1, false);
    ApSocket socket = {0}; CHECK(ap_socket_create(&socket, &metrics) == AP_OK);
    ApTransport io = ap_socket_transport(&socket);
    ApEndpoint endpoint = {.host = "localhost", .port = server.port};
    ApResult result = io.begin(io.context, &endpoint);
    while (result == AP_PENDING) { wait_io(&io); result = io.poll(io.context); }
    CHECK(result == AP_OK);
    size_t sent = 0;
    while (sent < 6) {
        size_t n; result = io.write(io.context, "hello!" + sent, 6 - sent, &n);
        if (result == AP_PENDING) { wait_io(&io); CHECK(io.poll(io.context) == AP_OK); }
        else { CHECK(result == AP_OK && n > 0); sent += n; }
    }
    uint8_t received[11] = {0}; size_t bytes = 0;
    while (bytes < sizeof(response) - 1) {
        size_t n; result = io.read(io.context, received + bytes, sizeof(received) - bytes, &n);
        if (result == AP_PENDING) { wait_io(&io); CHECK(io.poll(io.context) == AP_OK); }
        else { CHECK(result == AP_OK && n > 0); bytes += n; }
    }
    CHECK(bytes == sizeof(response) - 1 && memcmp(received, response, bytes) == 0 && received[10] == 0);
    CHECK(ap_socket_destroy(&socket) == AP_OK);
    CHECK(WaitForSingleObject(server.seen, 1000) == WAIT_OBJECT_0 && memcmp(server.request, "hello!", 6) == 0);
    test_ap_server_stop(&server); CHECK(metrics_balanced(&metrics));
    /* Cancellation retains async DNS storage until its event retires. */
    CHECK(ap_socket_create(&socket, &metrics) == AP_OK);
    endpoint.port = 1;
    result = io.begin(io.context, &endpoint); CHECK(result == AP_OK || result == AP_PENDING || result == AP_IO_ERROR);
    result = ap_socket_destroy(&socket);
    while (result == AP_PENDING) { wait_io(&io); result = ap_socket_destroy(&socket); }
    CHECK(result == AP_OK && metrics_balanced(&metrics));
}

static void native_bad_signature(void)
{
    Metrics metrics; CHECK(metrics_init(&metrics));
    uint8_t reply[sizeof(av_response)]; memcpy(reply, av_response, sizeof(reply)); reply[200] ^= 1;
    TestApServer server; test_ap_server_start(&server, &metrics, 0, reply, sizeof(reply), false);
    static _Alignas(ApSession) uint8_t storage[MEMORY_BUDGET_PROTOCOL];
    Arena arena; MetricsArena binding = {0};
    CHECK(arena_init(&arena, storage, sizeof(storage)) && metrics_attach_arena(&metrics, &arena, &binding));
    ApConfig config = {.metrics = &metrics, .version = 123456789, .max_attempts = 1};
    ApSession *session; CHECK(ap_session_create(&arena, &config, &session) == AP_OK);
    ApEndpoints endpoints = {.accesspoint = {{.host = "127.0.0.1", .port = server.port}}, .accesspoint_count = 1};
    CHECK(ap_start(session, &endpoints) == AP_OK);
    while (ap_state(session) != AP_STATE_CLOSED) {
        HANDLE handles[3]; size_t count = ap_wait_handles(session, handles, 3);
        CHECK(count != 0);
        DWORD waited = WaitForMultipleObjects((DWORD)count, handles, FALSE, ap_next_wait_ms(session));
        CHECK(waited == WAIT_OBJECT_0 || waited == WAIT_TIMEOUT);
        (void)ap_on_event(session, (ApEvent){waited == WAIT_TIMEOUT ? AP_EVENT_TIMER : AP_EVENT_SOCKET, ap_generation(session)});
    }
    CHECK(ap_result(session) == AP_BAD_SIGNATURE && crypto_balanced(&session->crypto));
    CHECK(ap_session_shutdown(session) == AP_OK); arena_dispose(&arena);
    test_ap_server_stop(&server); CHECK(metrics_balanced(&metrics));
}

void test_ap_socket(void) { native_transport(); native_bad_signature(); }
