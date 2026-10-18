#include "cdn_internal.h"
#include <wchar.h>
bool cdn_track(CdnClient *c, unsigned i, uintptr_t value)
{
    return !c->metrics || metrics_handle_acquire(c->metrics,&c->tracked[i],value);
}
void cdn_untrack(CdnClient *c, unsigned i)
{
    if (c->tracked[i].owner) { (void)metrics_handle_release(&c->tracked[i]); }
}
uint32_t cdn_remaining(const CdnClient *c)
{
    uint64_t now = GetTickCount64();
    return now >= c->deadline ? 0 : (uint32_t)(c->deadline - now);
}
HttpResult cdn_wait(CdnClient *c, HANDLE io)
{
    HANDLE events[] = {c->events[CDN_CANCEL],c->events[CDN_STOP],io};
    DWORD result = WaitForMultipleObjects(3,events,FALSE,cdn_remaining(c));
    if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 1) { return HTTP_CANCELLED; }
    if (result == WAIT_TIMEOUT) { return HTTP_TIMEOUT; }
    return result == WAIT_OBJECT_0 + 2 ? HTTP_OK : HTTP_OS_ERROR;
}
static HttpResult network(CdnClient *c)
{
    HttpResult result = cdn_wait(c,c->events[CDN_SOCKET]);
    if (result != HTTP_OK) { return result; }
    WSANETWORKEVENTS events;
    if (WSAEnumNetworkEvents(c->socket,c->events[CDN_SOCKET],&events)) { return HTTP_OS_ERROR; }
    if ((events.lNetworkEvents & FD_CONNECT) && events.iErrorCode[FD_CONNECT_BIT]) { return HTTP_OS_ERROR; }
    return HTTP_OK;
}
static HttpResult allowed(CdnClient *c)
{
    if (WaitForSingleObject(c->events[CDN_CANCEL],0) == WAIT_OBJECT_0 ||
        WaitForSingleObject(c->events[CDN_STOP],0) == WAIT_OBJECT_0) { return HTTP_CANCELLED; }
    return cdn_remaining(c) ? HTTP_OK : HTTP_TIMEOUT;
}
HttpResult cdn_socket_send(CdnClient *c, const void *data, size_t bytes)
{
    size_t sent = 0;
    while (sent < bytes) {
        HttpResult result = allowed(c); if (result != HTTP_OK) { return result; }
        int n = send(c->socket,(const char *)data + sent,(int)(bytes - sent),0);
        if (n > 0) { sent += (size_t)n; continue; }
        if (!n || WSAGetLastError() != WSAEWOULDBLOCK) { return HTTP_OS_ERROR; }
        result = network(c); if (result != HTTP_OK) { return result; }
    }
    return HTTP_OK;
}
HttpResult cdn_socket_read(CdnClient *c, void *data, size_t capacity, size_t *bytes)
{
    *bytes = 0;
    for (;;) {
        HttpResult result = allowed(c); if (result != HTTP_OK) { return result; }
        int n = recv(c->socket,data,(int)capacity,0);
        if (n >= 0) { *bytes = (size_t)n; return HTTP_OK; }
        if (WSAGetLastError() != WSAEWOULDBLOCK) { return HTTP_OS_ERROR; }
        result = network(c); if (result != HTTP_OK) { return result; }
    }
}
typedef struct CdnAddress { SOCKADDR_STORAGE address; int bytes; } CdnAddress;
static HttpResult addresses(CdnClient *c, CdnAddress list[8], size_t *count)
{
    *count = 0; IN_ADDR v4; IN6_ADDR v6;
    if (InetPtonW(AF_INET,c->url.host,&v4) == 1) {
        SOCKADDR_IN a = {0}; a.sin_family = AF_INET; a.sin_addr = v4; a.sin_port = htons(c->url.port);
        memcpy(&list[0].address,&a,sizeof(a)); list[0].bytes = sizeof(a); *count = 1; return HTTP_OK;
    }
    if (InetPtonW(AF_INET6,c->url.host,&v6) == 1) {
        SOCKADDR_IN6 a = {0}; a.sin6_family = AF_INET6; a.sin6_addr = v6; a.sin6_port = htons(c->url.port);
        memcpy(&list[0].address,&a,sizeof(a)); list[0].bytes = sizeof(a); *count = 1; return HTTP_OK;
    }
    wchar_t port[8]; if (swprintf_s(port,8,L"%u",(unsigned)c->url.port) <= 0) { return HTTP_INVALID; }
    ADDRINFOEXW hints = {0}, *result = NULL; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
    OVERLAPPED operation = {0}; operation.hEvent = c->events[CDN_DNS]; ResetEvent(operation.hEvent);
    HANDLE cancel = NULL; struct timeval timeout = {(long)(cdn_remaining(c)/1000 + 1),0};
    int status = GetAddrInfoExW(c->url.host,port,NS_DNS,NULL,&hints,&result,&timeout,&operation,NULL,&cancel);
    HttpResult outcome = HTTP_OK;
    if (status == WSA_IO_PENDING) {
        outcome = cdn_wait(c,operation.hEvent);
        if (outcome != HTTP_OK) {
            (void)GetAddrInfoExCancel(&cancel);
            /* The stack OVERLAPPED/result slot remain owned until retirement.
             * A provider teardown stall retains the entire client at shutdown. */
            (void)WaitForSingleObject(operation.hEvent,INFINITE);
        }
        status = GetAddrInfoExOverlappedResult(&operation);
    }
    if (status && outcome == HTTP_OK) { outcome = HTTP_OS_ERROR; }
    if (outcome == HTTP_OK) {
        for (ADDRINFOEXW *p = result; p && *count < 8; p = p->ai_next) {
            if ((p->ai_family != AF_INET && p->ai_family != AF_INET6) || p->ai_addrlen > sizeof(SOCKADDR_STORAGE)) { continue; }
            memcpy(&list[*count].address,p->ai_addr,p->ai_addrlen); list[*count].bytes = (int)p->ai_addrlen; ++*count;
        }
        if (!*count) { outcome = HTTP_OS_ERROR; }
    }
    if (result) { FreeAddrInfoExW(result); } return outcome;
}
static void socket_close(CdnClient *c)
{
    if (c->socket != INVALID_SOCKET) {
        (void)shutdown(c->socket,SD_BOTH);
        if (closesocket(c->socket) == 0) { cdn_untrack(c,CDN_EVENTS); }
        c->socket = INVALID_SOCKET;
    }
}
HttpResult cdn_socket_connect(CdnClient *c)
{
    CdnAddress list[8] = {0}; size_t count; HttpResult result = addresses(c,list,&count);
    if (result != HTTP_OK) { return result; }
    for (size_t i = 0; i < count; ++i) {
        result = allowed(c); if (result != HTTP_OK) { return result; }
        c->socket = socket(list[i].address.ss_family,SOCK_STREAM,IPPROTO_TCP);
        if (c->socket == INVALID_SOCKET) { continue; }
        ResetEvent(c->events[CDN_SOCKET]);
        if (!cdn_track(c,CDN_EVENTS,(uintptr_t)c->socket) ||
            WSAEventSelect(c->socket,c->events[CDN_SOCKET],FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE)) { socket_close(c); return HTTP_OS_ERROR; }
        int connected = connect(c->socket,(SOCKADDR *)&list[i].address,list[i].bytes);
        if (!connected) { return HTTP_OK; }
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            result = network(c);
            if (result == HTTP_OK) {
                int error = 0, bytes = sizeof(error);
                if (!getsockopt(c->socket,SOL_SOCKET,SO_ERROR,(char *)&error,&bytes) && !error) { return HTTP_OK; }
            }
        }
        socket_close(c);
        if (result == HTTP_TIMEOUT || result == HTTP_CANCELLED) { return result; }
    }
    return HTTP_OS_ERROR;
}
void cdn_connection_close(CdnClient *c)
{
    if (c->context_ready) { (void)DeleteSecurityContext(&c->context); c->context_ready = false; }
    if (c->credential_ready) { (void)FreeCredentialsHandle(&c->credential); c->credential_ready = false; }
    socket_close(c);
    c->encrypted = c->plain_at = c->plain_bytes = c->extra_at = c->extra_bytes = 0;
}
