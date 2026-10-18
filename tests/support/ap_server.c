#include "ap_server.h"
#include "test_check.h"
#include <string.h>

static bool wait_network(TestApServer *s, SOCKET socket)
{
    HANDLE handles[] = {s->stop, s->network};
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, 5000);
    if (result == WAIT_OBJECT_0) { return false; }
    CHECK(result == WAIT_OBJECT_0 + 1);
    WSANETWORKEVENTS events;
    CHECK(WSAEnumNetworkEvents(socket, s->network, &events) == 0); return true;
}

static void close_peer(TestApServer *s)
{
    if (s->peer != INVALID_SOCKET) {
        CHECK(closesocket(s->peer) == 0); s->peer = INVALID_SOCKET;
        CHECK(metrics_handle_release(&s->leases[1]));
    }
}

static DWORD WINAPI serve(void *context)
{
    TestApServer *s = context;
    for (;;) {
        s->peer = accept(s->listener, NULL, NULL);
        if (s->peer != INVALID_SOCKET) { break; }
        if (WSAGetLastError() != WSAEWOULDBLOCK || !wait_network(s, s->listener)) { return 0; }
    }
    CHECK(metrics_handle_acquire(s->metrics, &s->leases[1], (uintptr_t)s->peer));
    CHECK(WSAEventSelect(s->peer, s->network, FD_READ | FD_WRITE | FD_CLOSE) == 0);
    size_t expected = s->expected_bytes == 0 ? 6 : s->expected_bytes;
    while (s->request_bytes < expected) {
        int n = recv(s->peer, (char *)s->request + s->request_bytes, (int)(expected - s->request_bytes), 0);
        if (n > 0) { s->request_bytes += (size_t)n; }
        else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK || !wait_network(s, s->peer)) { goto finished; }
        if (s->expected_bytes == 0 && s->request_bytes == 6) {
            CHECK(s->request[0] == 0 && s->request[1] == 4);
            expected = ((size_t)s->request[2] << 24) | ((size_t)s->request[3] << 16) |
                ((size_t)s->request[4] << 8) | s->request[5];
            CHECK(expected >= 6 && expected <= sizeof(s->request));
        }
    }
    CHECK(SetEvent(s->seen));
    if (s->held) {
        HANDLE handles[] = {s->stop, s->release};
        if (WaitForMultipleObjects(2, handles, FALSE, 5000) != WAIT_OBJECT_0 + 1) { goto finished; }
    }
    for (size_t offset = 0; offset < s->response_bytes;) {
        int n = send(s->peer, (const char *)s->response + offset, 1, 0);
        if (n > 0) { offset += (size_t)n; }
        else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK || !wait_network(s, s->peer)) { goto finished; }
    }
    shutdown(s->peer, SD_SEND);
finished:
    close_peer(s); return 0;
}

void test_ap_server_start(TestApServer *s, Metrics *metrics, size_t expected_bytes,
    const uint8_t *response, size_t response_bytes, bool held)
{
    *s = (TestApServer){0}; s->metrics = metrics; s->listener = s->peer = INVALID_SOCKET;
    s->expected_bytes = expected_bytes; s->response = response; s->response_bytes = response_bytes; s->held = held;
    CHECK(expected_bytes <= sizeof(s->request));
    WSADATA data; CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    s->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); CHECK(s->listener != INVALID_SOCKET);
    CHECK(metrics_handle_acquire(metrics, &s->leases[0], (uintptr_t)s->listener));
    struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(s->listener, (const struct sockaddr *)&address, sizeof(address)) == 0);
    int size = sizeof(address); CHECK(getsockname(s->listener, (struct sockaddr *)&address, &size) == 0);
    s->port = ntohs(address.sin_port); CHECK(listen(s->listener, 1) == 0);
    s->network = WSACreateEvent(); s->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->seen = CreateEventW(NULL, TRUE, FALSE, NULL); s->release = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE handles[] = {s->network, s->stop, s->seen, s->release};
    for (size_t i = 0; i < 4; ++i) { CHECK(handles[i] != NULL && metrics_handle_acquire(metrics, &s->leases[i + 2], (uintptr_t)handles[i])); }
    CHECK(WSAEventSelect(s->listener, s->network, FD_ACCEPT | FD_CLOSE) == 0);
    s->thread = CreateThread(NULL, 0, serve, s, 0, NULL);
    CHECK(s->thread != NULL && metrics_handle_acquire(metrics, &s->leases[6], (uintptr_t)s->thread));
}

void test_ap_server_stop(TestApServer *s)
{
    CHECK(SetEvent(s->stop) && WaitForSingleObject(s->thread, 5000) == WAIT_OBJECT_0);
    close_peer(s); CHECK(closesocket(s->listener) == 0 && metrics_handle_release(&s->leases[0]));
    CHECK(WSACloseEvent(s->network) && metrics_handle_release(&s->leases[2]));
    HANDLE handles[] = {s->stop, s->seen, s->release, s->thread};
    for (size_t i = 0; i < 4; ++i) { CHECK(CloseHandle(handles[i]) && metrics_handle_release(&s->leases[i + 3])); }
    CHECK(WSACleanup() == 0);
}
