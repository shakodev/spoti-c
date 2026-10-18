#include "dealer_internal.h"
#include <string.h>

enum { N_SENT = 1, N_HEADERS = 2, N_READ = 4, N_WRITE = 8, N_ERROR = 16 };
static void CALLBACK callback(HINTERNET handle, DWORD_PTR context, DWORD status, void *info, DWORD bytes)
{
    (void)handle; DealerNative *n = (DealerNative *)context; if (!n) { return; }
    AcquireSRWLockExclusive(&n->lock);
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { if (n->handles) { --n->handles; } }
    else if (!n->closing) {
        if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE) { n->flags |= N_SENT; }
        else if (status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) { n->flags |= N_HEADERS; }
        else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            n->flags |= N_ERROR; n->error = bytes == sizeof(WINHTTP_ASYNC_RESULT) ? ((WINHTTP_ASYNC_RESULT *)info)->dwError : ERROR_INVALID_DATA;
        } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE && bytes == sizeof(WINHTTP_WEB_SOCKET_STATUS)) {
            const WINHTTP_WEB_SOCKET_STATUS *s = info;
            n->bytes = s->dwBytesTransferred; n->type = s->eBufferType; n->flags |= N_READ; n->read_pending = false;
        } else if (status == WINHTTP_CALLBACK_STATUS_WRITE_COMPLETE) { n->flags |= N_WRITE; n->send_pending = false; }
    }
    SetEvent(n->event); ReleaseSRWLockExclusive(&n->lock);
}
static bool option(HINTERNET h, DWORD key, DWORD value) { return WinHttpSetOption(h, key, &value, sizeof(value)) != FALSE; }
static DealerResult native_close(void *context)
{
    DealerNative *n = context;
    AcquireSRWLockExclusive(&n->lock); n->closing = true; ReleaseSRWLockExclusive(&n->lock);
    if (n->socket) {
        (void)WinHttpWebSocketShutdown(n->socket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        WinHttpCloseHandle(n->socket); n->socket = NULL;
    }
    if (n->request) { WinHttpCloseHandle(n->request); n->request = NULL; }
    AcquireSRWLockExclusive(&n->lock); unsigned handles = n->handles; ResetEvent(n->event); ReleaseSRWLockExclusive(&n->lock);
    if (handles) { return DEALER_PENDING; }
    if (n->connection) { WinHttpCloseHandle(n->connection); n->connection = NULL; }
    if (n->session) { WinHttpCloseHandle(n->session); n->session = NULL; }
    if (n->event) { CloseHandle(n->event); n->event = NULL; }
    SecureZeroMemory(n->send, sizeof(n->send)); SecureZeroMemory(n->receive, sizeof(n->receive));
    return DEALER_OK;
}
static DealerResult native_open(void *context, const ApEndpoint *endpoint, const wchar_t *path)
{
    DealerNative *n = context; if (n->session || n->event) { return DEALER_INVALID; }
    bool fixture = n->fixture_loopback && !strcmp(endpoint->host, "127.0.0.1");
    memset(n, 0, sizeof(*n)); InitializeSRWLock(&n->lock);
    n->fixture_loopback = fixture;
    n->event = CreateEventW(NULL, TRUE, FALSE, NULL); if (!n->event) { return DEALER_IO; }
    n->session = WinHttpOpen(L"spoti-c/0.1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, WINHTTP_FLAG_ASYNC);
    if (!n->session || !WinHttpSetTimeouts(n->session, 10000, 10000, 10000, 10000)) { return DEALER_IO; }
    wchar_t host[AP_HOST_CHARS];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, endpoint->host, -1, host, AP_HOST_CHARS)) { return DEALER_INVALID; }
    n->connection = WinHttpConnect(n->session, host, endpoint->port, 0); if (!n->connection) { return DEALER_IO; }
    n->request = WinHttpOpenRequest(n->connection, L"GET", path, NULL, NULL, NULL, fixture ? 0 : WINHTTP_FLAG_SECURE);
    if (!n->request) { return DEALER_IO; }
    DWORD_PTR ctx = (DWORD_PTR)n;
    if (!WinHttpSetOption(n->request, WINHTTP_OPTION_CONTEXT_VALUE, &ctx, sizeof(ctx))) { return DEALER_IO; }
    if (WinHttpSetStatusCallback(n->request, callback, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) == WINHTTP_INVALID_STATUS_CALLBACK) { return DEALER_IO; }
    n->handles = 1;
    if (!option(n->request, WINHTTP_OPTION_DISABLE_FEATURE, WINHTTP_DISABLE_REDIRECTS) ||
        !WinHttpSetOption(n->request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0)) { return DEALER_IO; }
    n->opening = true;
    if (!WinHttpSendRequest(n->request, NULL, 0, NULL, 0, 0, ctx) && GetLastError() != ERROR_IO_PENDING) { return DEALER_IO; }
    return DEALER_PENDING;
}
static DealerResult receive(DealerNative *n)
{
    AcquireSRWLockExclusive(&n->lock); n->read_pending = true; n->need_read = false; ReleaseSRWLockExclusive(&n->lock);
    DWORD status = WinHttpWebSocketReceive(n->socket, n->receive, sizeof(n->receive), NULL, NULL);
    return status == NO_ERROR || status == ERROR_IO_PENDING ? DEALER_PENDING : DEALER_IO;
}
static DealerResult native_poll(void *context, DealerFrame *frame)
{
    DealerNative *n = context; unsigned flags;
    AcquireSRWLockExclusive(&n->lock); flags = n->flags;
    if (flags & N_ERROR) { n->flags &= ~N_ERROR; ReleaseSRWLockExclusive(&n->lock); return DEALER_IO; }
    unsigned selected = flags & N_SENT ? N_SENT : flags & N_HEADERS ? N_HEADERS : flags & N_WRITE ? N_WRITE : flags & N_READ ? N_READ : 0;
    n->flags &= ~selected; if (!n->flags) { ResetEvent(n->event); }
    unsigned bytes = n->bytes; WINHTTP_WEB_SOCKET_BUFFER_TYPE type = n->type; bool sending = n->send_pending;
    ReleaseSRWLockExclusive(&n->lock);
    if (selected == N_SENT) {
        if (!WinHttpReceiveResponse(n->request, NULL) && GetLastError() != ERROR_IO_PENDING) { return DEALER_IO; }
    } else if (selected == N_HEADERS) {
        DWORD status = 0, count = sizeof(status);
        if (!WinHttpQueryHeaders(n->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &count, NULL)) { return DEALER_IO; }
        if (status != 101) { frame->status = status; return status == 401 || status == 403 ? DEALER_AUTH : DEALER_IO; }
        n->socket = WinHttpWebSocketCompleteUpgrade(n->request, (DWORD_PTR)n); if (!n->socket) { return DEALER_IO; }
        AcquireSRWLockExclusive(&n->lock); ++n->handles; ReleaseSRWLockExclusive(&n->lock);
        WinHttpCloseHandle(n->request); n->request = NULL;
        if (!option(n->socket, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, 30000) ||
            !option(n->socket, WINHTTP_OPTION_WEB_SOCKET_CLOSE_TIMEOUT, 3000)) { return DEALER_IO; }
        n->opening = false; n->need_read = true; *frame = (DealerFrame){.type = DEALER_READY}; return DEALER_OK;
    } else if (selected == N_WRITE) { *frame = (DealerFrame){.type = DEALER_WRITTEN}; return DEALER_OK; }
    else if (selected == N_READ) {
        if (bytes > sizeof(n->receive)) { return DEALER_LIMIT; }
        *frame = (DealerFrame){.data = n->receive, .bytes = bytes,
            .type = type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ? DEALER_TEXT_PART :
                type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ? DEALER_TEXT_END :
                type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE ? DEALER_PEER_CLOSE : DEALER_BINARY};
        n->need_read = true; return DEALER_OK;
    }
    if (n->socket && n->need_read && !sending) { return receive(n); }
    return DEALER_PENDING;
}
static DealerResult native_send(void *context, const void *data, size_t bytes)
{
    DealerNative *n = context;
    if (!n->socket || bytes > sizeof(n->send)) { return DEALER_INVALID; }
    AcquireSRWLockExclusive(&n->lock);
    if (n->send_pending) { ReleaseSRWLockExclusive(&n->lock); return DEALER_FULL; }
    memcpy(n->send, data, bytes); n->send_pending = true; ReleaseSRWLockExclusive(&n->lock);
    DWORD status = WinHttpWebSocketSend(n->socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, n->send, (DWORD)bytes);
    return status == NO_ERROR || status == ERROR_IO_PENDING ? DEALER_OK : DEALER_IO;
}
static HANDLE native_event(void *context) { return ((DealerNative *)context)->event; }
const DealerTransport dealer_native_transport = {native_open, native_poll, native_send, native_close, native_event};
