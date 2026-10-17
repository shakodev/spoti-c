#include "fixture_http.h"
#include <stdio.h>
#include <string.h>

static bool close_socket(SOCKET *socket, MetricsHandle *lease)
{
    if (*socket == INVALID_SOCKET) { return true; }
    if (closesocket(*socket) != 0) { return false; }
    *socket = INVALID_SOCKET;
    return lease->owner == NULL || metrics_handle_release(lease);
}

static bool close_handle(HANDLE *handle, MetricsHandle *lease)
{
    if (*handle == NULL) { return true; }
    if (!CloseHandle(*handle)) { return false; }
    *handle = NULL;
    return lease->owner == NULL || metrics_handle_release(lease);
}

static bool cleanup(FixtureHttp *fixture)
{
    if (!close_socket(&fixture->peer, &fixture->peer_metric) ||
        !close_socket(&fixture->listener, &fixture->listener_metric)) { return false; }
    if (fixture->network != WSA_INVALID_EVENT) {
        if (!WSACloseEvent(fixture->network)) { return false; }
        fixture->network = WSA_INVALID_EVENT;
        if (fixture->network_metric.owner != NULL &&
            !metrics_handle_release(&fixture->network_metric)) { return false; }
    }
    if (!close_handle(&fixture->stop, &fixture->stop_metric) ||
        !close_handle(&fixture->seen, &fixture->seen_metric) ||
        !close_handle(&fixture->release, &fixture->release_metric) ||
        !close_handle(&fixture->thread, &fixture->thread_metric)) { return false; }
    if (fixture->winsock_started) {
        if (WSACleanup() != 0) { return false; }
        fixture->winsock_started = false;
    }
    return true;
}

static bool accept_one(FixtureHttp *fixture)
{
    HANDLE wait[2] = {fixture->stop, fixture->network};
    for (;;) {
        DWORD result = WaitForMultipleObjects(2, wait, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) { return false; }
        if (result != WAIT_OBJECT_0 + 1) { atomic_store(&fixture->error, GetLastError()); return false; }
        WSANETWORKEVENTS events;
        if (WSAEnumNetworkEvents(fixture->listener, fixture->network, &events) != 0) { return false; }
        fixture->peer = accept(fixture->listener, NULL, NULL);
        if (fixture->peer == INVALID_SOCKET) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) { continue; }
            atomic_store(&fixture->error, (DWORD)WSAGetLastError()); return false;
        }
        if (!metrics_handle_acquire(fixture->metrics, &fixture->peer_metric, (uintptr_t)fixture->peer)) {
            return false;
        }
        if (!close_socket(&fixture->listener, &fixture->listener_metric)) { return false; }
        return WSAEventSelect(fixture->peer, fixture->network, FD_READ | FD_WRITE | FD_CLOSE) == 0;
    }
}

static DWORD WINAPI serve(void *context)
{
    FixtureHttp *fixture = context;
    bool headers = false, released = !fixture->hold_response;
    size_t sent = 0;
    if (!accept_one(fixture)) { goto finish; }
    for (;;) {
        if (WaitForSingleObject(fixture->stop, 0) == WAIT_OBJECT_0) { break; }
        if (!headers) {
            size_t remaining = sizeof(fixture->request) - fixture->request_bytes - 1;
            if (remaining == 0) { atomic_store(&fixture->error, WSAEMSGSIZE); break; }
            int bytes = recv(fixture->peer, fixture->request + fixture->request_bytes, (int)remaining, 0);
            if (bytes > 0) {
                fixture->request_bytes += (size_t)bytes;
                fixture->request[fixture->request_bytes] = '\0';
                if (strstr(fixture->request, "\r\n\r\n") != NULL) {
                    headers = true;
                    SetEvent(fixture->seen);
                }
            } else if (bytes == 0) { break; }
            else if (WSAGetLastError() != WSAEWOULDBLOCK) { break; }
        }
        if (headers && released) {
            int bytes = send(fixture->peer, fixture->response + sent,
                (int)(fixture->response_bytes - sent), 0);
            if (bytes > 0) {
                sent += (size_t)bytes;
                if (sent == fixture->response_bytes) { shutdown(fixture->peer, SD_SEND); break; }
                continue;
            }
            if (bytes == 0 || WSAGetLastError() != WSAEWOULDBLOCK) { break; }
        }
        HANDLE wait[3] = {fixture->stop, fixture->network, fixture->release};
        DWORD result = WaitForMultipleObjects(released ? 2 : 3, wait, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0) { break; }
        if (result == WAIT_OBJECT_0 + 2) { released = true; continue; }
        if (result == WAIT_OBJECT_0 + 1) {
            WSANETWORKEVENTS events;
            if (WSAEnumNetworkEvents(fixture->peer, fixture->network, &events) != 0 ||
                (events.lNetworkEvents & FD_CLOSE) != 0) { break; }
        } else { atomic_store(&fixture->error, GetLastError()); break; }
    }
finish:
    (void)close_socket(&fixture->peer, &fixture->peer_metric);
    (void)close_socket(&fixture->listener, &fixture->listener_metric);
    return 0;
}

CoreResult fixture_http_start(FixtureHttp *fixture, Metrics *metrics,
    unsigned status, const void *body, size_t bytes, bool hold_response)
{
    WSADATA data;
    struct sockaddr_in address = {0};
    int address_bytes = sizeof(address);
    int header;
    if (fixture == NULL || metrics == NULL || fixture->winsock_started || fixture->thread != NULL ||
        status < 100 || status > 599 || bytes > FIXTURE_HTTP_BODY_MAX || (bytes != 0 && body == NULL)) {
        return CORE_INVALID;
    }
    *fixture = (FixtureHttp){.listener = INVALID_SOCKET, .peer = INVALID_SOCKET,
        .network = WSA_INVALID_EVENT, .metrics = metrics, .hold_response = hold_response};
    atomic_init(&fixture->error, 0);
    header = snprintf(fixture->response, sizeof(fixture->response),
        "HTTP/1.1 %u Fixture\r\nContent-Length: %zu\r\nConnection: close\r\n"
        "Content-Type: application/octet-stream\r\n\r\n", status, bytes);
    if (header < 0 || (size_t)header + bytes > sizeof(fixture->response)) { return CORE_INVALID; }
    if (bytes != 0) { memcpy(fixture->response + header, body, bytes); }
    fixture->response_bytes = (size_t)header + bytes;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { return CORE_OS_ERROR; }
    fixture->winsock_started = true;
    fixture->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fixture->listener == INVALID_SOCKET || !metrics_handle_acquire(metrics,
        &fixture->listener_metric, (uintptr_t)fixture->listener)) { goto fail; }
    BOOL exclusive = TRUE;
    if (setsockopt(fixture->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
        (const char *)&exclusive, sizeof(exclusive)) != 0) { goto fail; }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fixture->listener, (const struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fixture->listener, 1) != 0 ||
        getsockname(fixture->listener, (struct sockaddr *)&address, &address_bytes) != 0) { goto fail; }
    fixture->port = ntohs(address.sin_port);
    fixture->network = WSACreateEvent();
    if (fixture->network == WSA_INVALID_EVENT || !metrics_handle_acquire(metrics,
        &fixture->network_metric, (uintptr_t)fixture->network)) { goto fail; }
    if (WSAEventSelect(fixture->listener, fixture->network, FD_ACCEPT | FD_CLOSE) != 0) { goto fail; }
    fixture->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    fixture->seen = CreateEventW(NULL, TRUE, FALSE, NULL);
    fixture->release = CreateEventW(NULL, TRUE, FALSE, NULL);
    bool tracked = true;
    if (fixture->stop != NULL) { tracked &= metrics_handle_acquire(metrics,
        &fixture->stop_metric, (uintptr_t)fixture->stop); }
    if (fixture->seen != NULL) { tracked &= metrics_handle_acquire(metrics,
        &fixture->seen_metric, (uintptr_t)fixture->seen); }
    if (fixture->release != NULL) { tracked &= metrics_handle_acquire(metrics,
        &fixture->release_metric, (uintptr_t)fixture->release); }
    if (!tracked || fixture->stop == NULL || fixture->seen == NULL || fixture->release == NULL) { goto fail; }
    fixture->thread = CreateThread(NULL, 0, serve, fixture, 0, NULL);
    if (fixture->thread == NULL) { goto fail; }
    if (!metrics_handle_acquire(metrics, &fixture->thread_metric, (uintptr_t)fixture->thread)) {
        SetEvent(fixture->stop); WaitForSingleObject(fixture->thread, INFINITE); goto fail;
    }
    return CORE_OK;
fail:
    (void)cleanup(fixture);
    return CORE_OS_ERROR;
}

CoreResult fixture_http_stop(FixtureHttp *fixture)
{
    DWORD result;
    if (fixture == NULL) { return CORE_INVALID; }
    if (!fixture->winsock_started) { return CORE_CLOSED; }
    if (fixture->thread != NULL) {
        if (!SetEvent(fixture->stop)) { return CORE_OS_ERROR; }
        result = WaitForSingleObject(fixture->thread, 5000);
        if (result == WAIT_TIMEOUT) { return CORE_TIMEOUT; }
        if (result != WAIT_OBJECT_0) { return CORE_OS_ERROR; }
    }
    return cleanup(fixture) ? CORE_OK : CORE_OS_ERROR;
}

FixtureTransport fixture_http_transport(const FixtureHttp *fixture)
{
    return (FixtureTransport){.endpoint = {fixture->port, fixture->metrics},
        .exchange = fixture_http_exchange};
}
