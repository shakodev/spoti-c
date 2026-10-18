#include "ap_socket.h"
#include <string.h>
#include <wchar.h>

static ApResult close_socket(ApSocket *s)
{
    if (s->socket != INVALID_SOCKET) {
        if (closesocket(s->socket) != 0) { return AP_IO_ERROR; }
        s->socket = INVALID_SOCKET;
        if (s->socket_metric.owner != NULL) { (void)metrics_handle_release(&s->socket_metric); }
    }
    s->connected = s->connecting = false; return AP_OK;
}

static void free_addresses(ApSocket *s)
{
    if (s->addresses != NULL) { FreeAddrInfoExW(s->addresses); s->addresses = s->next = NULL; }
}

static ApResult connect_next(ApSocket *s)
{
    while (s->next != NULL && s->address_attempts++ < AP_ENDPOINT_COUNT) {
        PADDRINFOEXW address = s->next; s->next = address->ai_next;
        if (address->ai_family != AF_INET && address->ai_family != AF_INET6) { continue; }
        s->socket = socket(address->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if (s->socket == INVALID_SOCKET) { continue; }
        if (s->metrics != NULL && !metrics_handle_acquire(s->metrics, &s->socket_metric, (uintptr_t)s->socket)) {
            (void)close_socket(s); free_addresses(s); return AP_EXHAUSTED;
        }
        if (WSAEventSelect(s->socket, s->network, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE) != 0) {
            (void)close_socket(s); continue;
        }
        int result = connect(s->socket, address->ai_addr, (int)address->ai_addrlen);
        if (result == 0) { s->connected = true; free_addresses(s); return AP_OK; }
        int error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS) { s->connecting = true; return AP_PENDING; }
        (void)close_socket(s);
    }
    free_addresses(s); return AP_IO_ERROR;
}

static ApResult begin(void *context, const ApEndpoint *endpoint)
{
    ApSocket *s = context;
    if (s == NULL || !s->initialized || !ap_endpoint_valid(endpoint)) { return AP_INVALID; }
    if (s->dns_pending || s->socket != INVALID_SOCKET || s->addresses != NULL) { return AP_BAD_STATE; }
    size_t length = strlen(endpoint->host);
    for (size_t i = 0; i <= length; ++i) { s->host[i] = (wchar_t)(unsigned char)endpoint->host[i]; }
    if (swprintf_s(s->service, 6, L"%u", (unsigned)endpoint->port) <= 0) { return AP_INVALID; }
    ResetEvent(s->dns.hEvent); WSAResetEvent(s->network);
    HANDLE event = s->dns.hEvent;
    s->dns = (OVERLAPPED){0}; s->dns.hEvent = event;
    s->cancelling = false; s->address_attempts = 0;
    s->dns_hints = (ADDRINFOEXW){0};
    s->dns_hints.ai_family = AF_UNSPEC; s->dns_hints.ai_socktype = SOCK_STREAM; s->dns_hints.ai_protocol = IPPROTO_TCP;
    s->dns_hints.ai_flags = AI_NUMERICSERV;
    s->dns_timeout = (struct timeval){5, 0};
    int result = GetAddrInfoExW(s->host, s->service, NS_DNS, NULL, &s->dns_hints, &s->addresses,
        &s->dns_timeout, &s->dns, NULL, NULL);
    if (result == WSA_IO_PENDING) { s->dns_pending = true; return AP_PENDING; }
    if (result != 0) { free_addresses(s); return AP_IO_ERROR; }
    s->next = s->addresses; return connect_next(s);
}

static ApResult poll(void *context)
{
    ApSocket *s = context;
    if (s == NULL || !s->initialized) { return AP_INVALID; }
    if (s->dns_pending) {
        DWORD result = WaitForSingleObject(s->dns.hEvent, 0);
        if (result == WAIT_TIMEOUT) { return AP_PENDING; }
        if (result != WAIT_OBJECT_0) { return AP_IO_ERROR; }
        int error = GetAddrInfoExOverlappedResult(&s->dns);
        s->dns_pending = false;
        if (s->cancelling) { free_addresses(s); return AP_CANCELLED; }
        if (error != 0) { free_addresses(s); return AP_IO_ERROR; }
        s->next = s->addresses; return connect_next(s);
    }
    if (s->socket == INVALID_SOCKET) { return AP_CLOSED; }
    WSANETWORKEVENTS events;
    if (WSAEnumNetworkEvents(s->socket, s->network, &events) != 0) { return AP_IO_ERROR; }
    if (s->connecting) {
        if ((events.lNetworkEvents & FD_CONNECT) == 0) { return AP_PENDING; }
        if (events.iErrorCode[FD_CONNECT_BIT] != 0) {
            (void)close_socket(s); return connect_next(s);
        }
        s->connecting = false; s->connected = true; free_addresses(s);
    }
    /* FD_CLOSE may accompany unread final bytes; recv decides EOF after drain. */
    return s->connected ? AP_OK : AP_PENDING;
}

static ApResult read_bytes(void *context, void *bytes, size_t capacity, size_t *received)
{
    ApSocket *s = context;
    if (received == NULL) { return AP_INVALID; }
    *received = 0;
    if (s == NULL || !s->initialized || bytes == NULL) { return AP_INVALID; }
    if (!s->connected) { return AP_BAD_STATE; }
    if (capacity == 0 || capacity > INT_MAX) { return AP_INVALID; }
    int result = recv(s->socket, bytes, (int)capacity, 0);
    if (result > 0) { *received = (size_t)result; return AP_OK; }
    if (result == 0) { return AP_CLOSED; }
    return WSAGetLastError() == WSAEWOULDBLOCK ? AP_PENDING : AP_IO_ERROR;
}

static ApResult write_bytes(void *context, const void *bytes, size_t size, size_t *sent)
{
    ApSocket *s = context;
    if (sent == NULL) { return AP_INVALID; }
    *sent = 0;
    if (s == NULL || !s->initialized || bytes == NULL) { return AP_INVALID; }
    if (!s->connected) { return AP_BAD_STATE; }
    if (size == 0 || size > INT_MAX) { return AP_INVALID; }
    int result = send(s->socket, bytes, (int)size, 0);
    if (result > 0) { *sent = (size_t)result; return AP_OK; }
    return result < 0 && WSAGetLastError() == WSAEWOULDBLOCK ? AP_PENDING : AP_IO_ERROR;
}

static ApResult stop(void *context)
{
    ApSocket *s = context;
    if (s == NULL || !s->initialized) { return AP_INVALID; }
    if (s->dns_pending) {
        /* Stop admission immediately, but let the bounded OS query finish.
         * GetAddrInfoExCancel followed by final WSACleanup raced DNSAPI worker
         * teardown under ASan on the target Windows build. All input, timeout,
         * hints, event and result storage stays alive through natural retirement. */
        s->cancelling = true;
        if (WaitForSingleObject(s->dns.hEvent, 0) != WAIT_OBJECT_0) { return AP_PENDING; }
        (void)GetAddrInfoExOverlappedResult(&s->dns);
        s->dns_pending = false;
    }
    free_addresses(s);
    ApResult result = close_socket(s);
    WSAResetEvent(s->network); ResetEvent(s->dns.hEvent);
    return result;
}

static size_t handles(void *context, HANDLE *output, size_t capacity)
{
    ApSocket *s = context;
    if (capacity == 0 || output == NULL) { return 0; }
    if (s->dns_pending) { output[0] = s->dns.hEvent; return 1; }
    if (s->socket != INVALID_SOCKET) { output[0] = s->network; return 1; }
    return 0;
}

ApTransport ap_socket_transport(ApSocket *s)
{
    return (ApTransport){s, begin, poll, read_bytes, write_bytes, stop, handles};
}

ApResult ap_socket_create(ApSocket *s, Metrics *metrics)
{
    if (s == NULL || s->initialized) { return AP_INVALID; }
    *s = (ApSocket){0}; s->socket = INVALID_SOCKET; s->metrics = metrics;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { return AP_IO_ERROR; }
    s->initialized = true;
    s->network = WSACreateEvent(); s->dns.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    bool tracked = true;
    if (metrics != NULL) {
        if (s->network != WSA_INVALID_EVENT) { tracked &= metrics_handle_acquire(metrics, &s->network_metric, (uintptr_t)s->network); }
        if (s->dns.hEvent != NULL) { tracked &= metrics_handle_acquire(metrics, &s->dns_metric, (uintptr_t)s->dns.hEvent); }
    }
    if (!tracked || s->network == WSA_INVALID_EVENT || s->dns.hEvent == NULL) {
        (void)ap_socket_destroy(s); return AP_IO_ERROR;
    }
    return AP_OK;
}

ApResult ap_socket_destroy(ApSocket *s)
{
    if (s == NULL || !s->initialized) { return AP_INVALID; }
    ApResult result = stop(s);
    if (result != AP_OK) { return result; }
    if (s->network != WSA_INVALID_EVENT) {
        if (!WSACloseEvent(s->network)) { return AP_IO_ERROR; }
        s->network = WSA_INVALID_EVENT;
        if (s->network_metric.owner != NULL) { (void)metrics_handle_release(&s->network_metric); }
    }
    if (s->dns.hEvent != NULL) {
        if (!CloseHandle(s->dns.hEvent)) { return AP_IO_ERROR; }
        s->dns.hEvent = NULL;
        if (s->dns_metric.owner != NULL) { (void)metrics_handle_release(&s->dns_metric); }
    }
    if (WSACleanup() != 0) { return AP_IO_ERROR; }
    s->initialized = false; return AP_OK;
}
