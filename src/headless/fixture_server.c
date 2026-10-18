#include "fixture_server.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static bool wait_socket(HeadlessServer *s, SOCKET socket)
{
    HANDLE handles[] = {s->stop,s->network};
    if (WaitForMultipleObjects(2, handles, FALSE, 10000) != WAIT_OBJECT_0 + 1) { return false; }
    WSANETWORKEVENTS events; return WSAEnumNetworkEvents(socket, s->network, &events) == 0;
}
static bool send_all(HeadlessServer *s, const void *data, size_t bytes)
{
    size_t offset = 0;
    while (offset < bytes) {
        int n = send(s->peer, (const char *)data + offset, (int)(bytes - offset), 0);
        if (n > 0) { offset += (size_t)n; }
        else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK || !wait_socket(s, s->peer)) { return false; }
    }
    return true;
}
static bool response(HeadlessServer *s)
{
    char request[4096] = {0}; size_t used = 0;
    while (!strstr(request, "\r\n\r\n")) {
        if (used == sizeof(request) - 1) { return false; }
        int n = recv(s->peer, request + used, (int)(sizeof(request) - used - 1), 0);
        if (n > 0) { used += (size_t)n; request[used] = 0; }
        else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK || !wait_socket(s, s->peer)) { return false; }
    }
    /* This server is an explicit fixture transport; no sensitive headers or
     * request contents are retained. Accept only the one ciphertext resource. */
    if (strncmp(request, "GET /audio HTTP/1.1\r\n", 21)) { return false; }
    const char *range = NULL;
    for (const char *p = request; (p = strstr(p, "\r\n")) != NULL; p += 2) {
        if (!_strnicmp(p + 2, "Range: bytes=", 13)) { range = p + 15; break; }
    }
    unsigned long long first, last;
    if (!range || sscanf_s(range, "%llu-%llu", &first, &last) != 2 || first > last || first >= s->bytes) { return false; }
    if (last >= s->bytes) { last = s->bytes - 1; }
    atomic_fetch_add(&s->requests, 1);
    unsigned fault = atomic_load(&s->fault);
    char header[300];
    int n = sprintf_s(header, sizeof(header), "HTTP/1.1 206 Partial Content\r\nContent-Type: application/octet-stream\r\n"
        "Content-Range: bytes %llu-%llu/%llu\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",
        first + (fault == 1),last,(unsigned long long)s->bytes,last-first+1);
    if (n <= 0 || !send_all(s, header, (size_t)n)) { return false; }
    LARGE_INTEGER pos; pos.QuadPart = (LONGLONG)first;
    if (!SetFilePointerEx(s->file, pos, NULL, FILE_BEGIN)) { return false; }
    uint64_t left = last - first + 1; if (fault == 2) { left /= 2; }
    uint8_t buffer[4096];
    while (left) {
        DWORD take = left > sizeof(buffer) ? sizeof(buffer) : (DWORD)left, got;
        if (!ReadFile(s->file, buffer, take, &got, NULL) || got != take) { return false; }
        if (fault == 3 && first == 0) { buffer[167] ^= 0xff; }
        if (!send_all(s, buffer, got)) { return false; } left -= got; first += got;
    }
    return true;
}
static DWORD WINAPI serve(void *ctx)
{
    HeadlessServer *s = ctx;
    while (WaitForSingleObject(s->stop, 0) == WAIT_TIMEOUT) {
        s->peer = accept(s->listener, NULL, NULL);
        if (s->peer == INVALID_SOCKET) {
            if (WSAGetLastError() != WSAEWOULDBLOCK || !wait_socket(s, s->listener)) { break; }
            continue;
        }
        if (WSAEventSelect(s->peer, s->network, FD_READ | FD_WRITE | FD_CLOSE) == 0) { (void)response(s); }
        shutdown(s->peer, SD_BOTH); closesocket(s->peer); s->peer = INVALID_SOCKET;
        if (WSAEventSelect(s->listener, s->network, FD_ACCEPT | FD_CLOSE)) { break; }
    }
    return 0;
}
bool headless_server_open(HeadlessServer *s, const wchar_t *path)
{
    if (!s || !path) { return false; }
    *s = (HeadlessServer){.listener = INVALID_SOCKET, .peer = INVALID_SOCKET, .file = INVALID_HANDLE_VALUE};
    WSADATA data;
    if (WSAStartup(MAKEWORD(2,2), &data)) { return false; } s->winsock = true;
    s->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER bytes;
    if (s->file == INVALID_HANDLE_VALUE || !GetFileSizeEx(s->file, &bytes) || bytes.QuadPart <= 172) { goto fail; }
    s->bytes = (uint64_t)bytes.QuadPart;
    s->stop = CreateEventW(NULL, TRUE, FALSE, NULL); s->network = WSACreateEvent();
    s->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!s->stop || !s->network || s->listener == INVALID_SOCKET) { goto fail; }
    struct sockaddr_in a = {0}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s->listener, (struct sockaddr *)&a, sizeof(a))) { goto fail; }
    int length = sizeof(a);
    if (getsockname(s->listener, (struct sockaddr *)&a, &length) || listen(s->listener, 4) ||
        WSAEventSelect(s->listener, s->network, FD_ACCEPT | FD_CLOSE)) { goto fail; }
    s->port = ntohs(a.sin_port); s->thread = CreateThread(NULL, 0, serve, s, 0, NULL);
    if (s->thread) { return true; }
fail:
    (void)headless_server_close(s); return false;
}
bool headless_server_close(HeadlessServer *s)
{
    bool ok = true;
    if (s->stop) { ok &= SetEvent(s->stop) != 0; }
    if (s->thread) {
        if (WaitForSingleObject(s->thread, 15000) != WAIT_OBJECT_0) { return false; }
        ok &= CloseHandle(s->thread) != 0;
    }
    if (s->peer != INVALID_SOCKET) { ok &= closesocket(s->peer) == 0; }
    if (s->listener != INVALID_SOCKET) { ok &= closesocket(s->listener) == 0; }
    if (s->network) { ok &= WSACloseEvent(s->network) != 0; }
    if (s->stop) { ok &= CloseHandle(s->stop) != 0; }
    if (s->file != INVALID_HANDLE_VALUE) { ok &= CloseHandle(s->file) != 0; }
    if (s->winsock) { ok &= WSACleanup() == 0; }
    *s = (HeadlessServer){.listener = INVALID_SOCKET, .peer = INVALID_SOCKET, .file = INVALID_HANDLE_VALUE}; return ok;
}
