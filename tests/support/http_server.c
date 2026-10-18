#include "http_server.h"
#include "test_check.h"
#include <string.h>

static void close_peer(TestHttpServer *s)
{
    if (s->peer != INVALID_SOCKET) {
        CHECK(closesocket(s->peer) == 0); s->peer = INVALID_SOCKET;
        CHECK(metrics_handle_release(&s->leases[1]));
    }
}

static bool network_wait(TestHttpServer *s, SOCKET socket)
{
    HANDLE handles[2] = {s->stop, s->network};
    DWORD result = WaitForMultipleObjects(2, handles, FALSE, 5000);
    if (result == WAIT_OBJECT_0) { return false; }
    if (result != WAIT_OBJECT_0 + 1) { atomic_store(&s->error, ERROR_TIMEOUT); return false; }
    WSANETWORKEVENTS events;
    if (WSAEnumNetworkEvents(socket, s->network, &events) != 0) { return false; }
    return true;
}

static bool gate(TestHttpServer *s, HANDLE release)
{
    HANDLE handles[2] = {s->stop, release};
    return WaitForMultipleObjects(2, handles, FALSE, 5000) == WAIT_OBJECT_0 + 1;
}

static bool send_bytes(TestHttpServer *s, const void *body, size_t length, size_t fragment, unsigned char fill)
{
    unsigned char generated[4096];
    memset(generated, fill, sizeof(generated));
    size_t offset = 0;
    while (offset < length) {
        if (WaitForSingleObject(s->stop, 0) == WAIT_OBJECT_0) { return false; }
        size_t n = length - offset;
        if (n > sizeof(generated)) { n = sizeof(generated); }
        if (fragment != 0 && n > fragment) { n = fragment; }
        const char *p = body == NULL ? (const char *)generated : (const char *)body + offset;
        int sent = send(s->peer, p, (int)n, 0);
        if (sent > 0) { offset += (size_t)sent; continue; }
        if (sent == 0 || WSAGetLastError() != WSAEWOULDBLOCK || !network_wait(s, s->peer)) { return false; }
    }
    return true;
}

static DWORD WINAPI serve(void *context)
{
    TestHttpServer *s = context;
    for (size_t i = 0; i < s->response_count; ++i) {
        const TestHttpResponse *r = &s->responses[i];
        for (;;) {
            if (WaitForSingleObject(s->stop, 0) == WAIT_OBJECT_0) { return 0; }
            s->peer = accept(s->listener, NULL, NULL);
            if (s->peer != INVALID_SOCKET) { break; }
            if (WSAGetLastError() != WSAEWOULDBLOCK || !network_wait(s, s->listener)) { return 0; }
        }
        CHECK(metrics_handle_acquire(s->metrics, &s->leases[1], (uintptr_t)s->peer));
        CHECK(WSAEventSelect(s->peer, s->network, FD_READ | FD_WRITE | FD_CLOSE) == 0);
        size_t used = 0;
        while (strstr(s->requests[i], "\r\n\r\n") == NULL) {
            if (used == sizeof(s->requests[i]) - 1) { goto finished; }
            int n = recv(s->peer, s->requests[i] + used, (int)(sizeof(s->requests[i]) - used - 1), 0);
            if (n > 0) { used += (size_t)n; s->requests[i][used] = '\0'; continue; }
            if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK || !network_wait(s, s->peer)) { goto finished; }
        }
        atomic_fetch_add(&s->accepted, 1); SetEvent(s->seen);
        if (r->hold_headers && !gate(s, s->release_headers)) { goto finished; }
        if (!send_bytes(s, r->headers, strlen(r->headers), 0, 0)) { goto finished; }
        SetEvent(s->headers_sent);
        if (r->hold_body && !gate(s, s->release_body)) { goto finished; }
        (void)send_bytes(s, r->body, r->bytes, r->fragment, r->fill);
        shutdown(s->peer, SD_SEND);
        close_peer(s);
        CHECK(WSAEventSelect(s->listener, s->network, FD_ACCEPT | FD_CLOSE) == 0);
    }
finished:
    close_peer(s); return 0;
}

bool test_http_server_start(TestHttpServer *s, Metrics *metrics,
    const TestHttpResponse *responses, size_t count)
{
    CHECK(s != NULL && metrics != NULL && responses != NULL && count > 0 && count <= TEST_HTTP_RESPONSES);
    *s = (TestHttpServer){0};
    s->listener = s->peer = INVALID_SOCKET;
    s->metrics = metrics; s->responses = responses; s->response_count = count;
    atomic_init(&s->accepted, 0); atomic_init(&s->error, 0);
    WSADATA data;
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0); s->winsock = true;
    s->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(s->listener != INVALID_SOCKET);
    CHECK(metrics_handle_acquire(metrics, &s->leases[0], (uintptr_t)s->listener));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(s->listener, (const struct sockaddr *)&address, sizeof(address)) == 0);
    int bytes = sizeof(address);
    CHECK(getsockname(s->listener, (struct sockaddr *)&address, &bytes) == 0);
    s->port = ntohs(address.sin_port);
    CHECK(listen(s->listener, 8) == 0);
    s->network = WSACreateEvent();
    s->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->seen = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->headers_sent = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->release_headers = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->release_body = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE handles[] = {s->network, s->stop, s->seen, s->headers_sent, s->release_headers, s->release_body};
    for (size_t i = 0; i < 6; ++i) {
        CHECK(handles[i] != NULL && metrics_handle_acquire(metrics, &s->leases[i + 2], (uintptr_t)handles[i]));
    }
    CHECK(WSAEventSelect(s->listener, s->network, FD_ACCEPT | FD_CLOSE) == 0);
    s->thread = CreateThread(NULL, 0, serve, s, 0, NULL);
    CHECK(s->thread != NULL && metrics_handle_acquire(metrics, &s->leases[8], (uintptr_t)s->thread));
    return true;
}

void test_http_server_stop(TestHttpServer *s)
{
    CHECK(SetEvent(s->stop));
    CHECK(WaitForSingleObject(s->thread, 5000) == WAIT_OBJECT_0);
    close_peer(s);
    CHECK(closesocket(s->listener) == 0); s->listener = INVALID_SOCKET;
    CHECK(metrics_handle_release(&s->leases[0]));
    CHECK(WSACloseEvent(s->network)); CHECK(metrics_handle_release(&s->leases[2]));
    HANDLE handles[] = {s->stop, s->seen, s->headers_sent, s->release_headers, s->release_body, s->thread};
    for (size_t i = 0; i < 6; ++i) {
        CHECK(CloseHandle(handles[i])); CHECK(metrics_handle_release(&s->leases[i + 3]));
    }
    CHECK(WSACleanup() == 0); s->winsock = false;
    CHECK(atomic_load(&s->error) == 0);
}
