#include "fixture_http.h"
#include "core/clock.h"

CoreResult fixture_http_exchange(const FixtureEndpoint *endpoint, const void *request,
    size_t request_bytes, HANDLE cancel, void *response, size_t capacity, size_t *received)
{
    WSADATA data;
    SOCKET socket_value = INVALID_SOCKET;
    WSAEVENT network = WSA_INVALID_EVENT;
    MetricsHandle socket_metric = {0}, event_metric = {0};
    struct sockaddr_in address = {0};
    size_t sent = 0;
    bool connected = false;
    CoreResult result = CORE_OS_ERROR;
    if (received == NULL) { return CORE_INVALID; }
    *received = 0;
    if (endpoint == NULL || endpoint->port == 0 || endpoint->metrics == NULL || request == NULL ||
        request_bytes == 0 || request_bytes >= FIXTURE_HTTP_HEADER_MAX ||
        response == NULL || capacity == 0 || capacity > FIXTURE_HTTP_RESPONSE_MAX) { return CORE_INVALID; }
    if (cancel != NULL && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) { return CORE_CLOSED; }
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { return CORE_OS_ERROR; }
    socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_value == INVALID_SOCKET || !metrics_handle_acquire(endpoint->metrics,
        &socket_metric, (uintptr_t)socket_value)) { goto finish; }
    network = WSACreateEvent();
    if (network == WSA_INVALID_EVENT || !metrics_handle_acquire(endpoint->metrics,
        &event_metric, (uintptr_t)network)) { goto finish; }
    if (WSAEventSelect(socket_value, network, FD_CONNECT | FD_WRITE | FD_READ | FD_CLOSE) != 0) {
        goto finish;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(endpoint->port);
    if (connect(socket_value, (const struct sockaddr *)&address, sizeof(address)) == 0) {
        connected = true;
    } else if (WSAGetLastError() != WSAEWOULDBLOCK) { goto finish; }
    uint64_t deadline = core_deadline_after(core_clock_now(NULL), 3000);
    for (;;) {
        if (cancel != NULL && WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) {
            result = CORE_CLOSED; break;
        }
        if (connected && sent < request_bytes) {
            int bytes = send(socket_value, (const char *)request + sent, (int)(request_bytes - sent), 0);
            if (bytes > 0) { sent += (size_t)bytes; continue; }
            if (bytes == 0 || WSAGetLastError() != WSAEWOULDBLOCK) { break; }
        }
        if (connected && sent == request_bytes) {
            if (*received == capacity) { result = CORE_FULL; break; }
            int bytes = recv(socket_value, (char *)response + *received, (int)(capacity - *received), 0);
            if (bytes > 0) { *received += (size_t)bytes; continue; }
            if (bytes == 0) { result = *received == 0 ? CORE_OS_ERROR : CORE_OK; break; }
            if (WSAGetLastError() != WSAEWOULDBLOCK) { break; }
        }
        HANDLE wait[2] = {network, cancel};
        DWORD waited = WaitForMultipleObjects(cancel == NULL ? 1 : 2, wait, FALSE,
            core_deadline_wait(core_clock_now(NULL), deadline));
        if (waited == WAIT_TIMEOUT) { result = CORE_TIMEOUT; break; }
        if (waited == WAIT_OBJECT_0 + 1) { result = CORE_CLOSED; break; }
        if (waited != WAIT_OBJECT_0) { break; }
        WSANETWORKEVENTS events;
        if (WSAEnumNetworkEvents(socket_value, network, &events) != 0) { break; }
        if ((events.lNetworkEvents & FD_CONNECT) != 0) {
            if (events.iErrorCode[FD_CONNECT_BIT] != 0) { break; }
            connected = true;
        }
        /* recv drains any final response bytes before observing EOF. */
    }
finish:
    if (socket_value != INVALID_SOCKET) {
        if (closesocket(socket_value) != 0) { result = CORE_OS_ERROR; }
        else if (socket_metric.owner != NULL && !metrics_handle_release(&socket_metric)) { result = CORE_OS_ERROR; }
    }
    if (network != WSA_INVALID_EVENT) {
        if (!WSACloseEvent(network)) { result = CORE_OS_ERROR; }
        else if (event_metric.owner != NULL && !metrics_handle_release(&event_metric)) { result = CORE_OS_ERROR; }
    }
    if (WSACleanup() != 0) { result = CORE_OS_ERROR; }
    return result;
}
