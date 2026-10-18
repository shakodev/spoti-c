#include "oauth_internal.h"
#include <string.h>
#include <stdio.h>

static void socket_close(OAuthFlow *f, SOCKET *socket)
{
    if (*socket == INVALID_SOCKET) { return; }
    if (closesocket(*socket) == 0) { --f->live_sockets; } else { f->cleanup_failed = true; }
    *socket = INVALID_SOCKET;
}
static void peer_close(OAuthFlow *f)
{
    socket_close(f, &f->peer); f->peer_deadline = CORE_CLOCK_NEVER;
    if (f->peer_event != WSA_INVALID_EVENT && !WSAResetEvent(f->peer_event)) { f->cleanup_failed = true; }
    SecureZeroMemory(f->request, sizeof(f->request)); f->request_bytes = 0;
    if (f->listener != INVALID_SOCKET && WSAEventSelect(f->listener, f->listen_event, FD_ACCEPT | FD_CLOSE) != 0) {
        f->cleanup_failed = true;
    }
}
void oauth_loopback_close(OAuthFlow *f)
{
    socket_close(f, &f->listener); peer_close(f);
    WSAEVENT *events[2] = {&f->listen_event, &f->peer_event};
    for (size_t i = 0; i < 2; ++i) {
        if (*events[i] != WSA_INVALID_EVENT) {
            if (WSACloseEvent(*events[i])) { --f->live_events; } else { f->cleanup_failed = true; }
            *events[i] = WSA_INVALID_EVENT;
        }
    }
    if (f->winsock) { if (WSACleanup() != 0) { f->cleanup_failed = true; } f->winsock = false; }
}
OAuthResult oauth_loopback_open(OAuthFlow *f)
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { return OAUTH_IO_ERROR; }
    f->winsock = true;
    f->listener = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (f->listener == INVALID_SOCKET) { return OAUTH_IO_ERROR; }
    ++f->live_sockets;
    BOOL exclusive = TRUE;
    if (setsockopt(f->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof(exclusive)) != 0) {
        return OAUTH_IO_ERROR;
    }
    struct sockaddr_in address = {0}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(f->port);
    if (bind(f->listener, (const struct sockaddr *)&address, sizeof(address)) != 0 || listen(f->listener, 2) != 0) {
        return OAUTH_IO_ERROR;
    }
    int bytes = sizeof(address);
    if (getsockname(f->listener, (struct sockaddr *)&address, &bytes) != 0) { return OAUTH_IO_ERROR; }
    f->port = ntohs(address.sin_port);
    if (sprintf_s(f->host, sizeof(f->host), "127.0.0.1:%u", (unsigned)f->port) < 0 ||
        sprintf_s(f->redirect, sizeof(f->redirect), "http://%s%s", f->host, f->path) < 0) { return OAUTH_FULL; }
    f->listen_event = WSACreateEvent(); if (f->listen_event == WSA_INVALID_EVENT) { return OAUTH_IO_ERROR; }
    ++f->live_events;
    f->peer_event = WSACreateEvent(); if (f->peer_event == WSA_INVALID_EVENT) { return OAUTH_IO_ERROR; }
    ++f->live_events;
    return WSAEventSelect(f->listener, f->listen_event, FD_ACCEPT | FD_CLOSE) == 0 ? OAUTH_OK : OAUTH_IO_ERROR;
}
static int hex(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}
static bool decode(const char *p, size_t n, char *out, size_t cap)
{
    size_t at = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c == '%') {
            if (n - i < 3 || hex(p[i + 1]) < 0 || hex(p[i + 2]) < 0) { return false; }
            c = (unsigned char)((hex(p[i + 1]) << 4) | hex(p[i + 2])); i += 2;
        } else if (c == '+') { c = ' '; }
        if (c < 32 || c >= 127 || at + 1 >= cap) { return false; }
        out[at++] = (char)c;
    }
    out[at] = 0; return true;
}
static OAuthResult callback_target(OAuthFlow *f, const char *p, size_t bytes)
{
    size_t path_bytes = strlen(f->path);
    if (bytes <= path_bytes || memcmp(p, f->path, path_bytes) != 0 || p[path_bytes] != '?') { return OAUTH_INVALID; }
    p += path_bytes + 1; bytes -= path_bytes + 1;
    char state[44] = {0}, code[1025] = {0}, value[1025] = {0};
    bool have_state = false, have_code = false, have_error = false;
    OAuthResult result = OAUTH_INVALID;
    /* Reject repeated names, including percent-encoded aliases. At most 16
     * parameters; no quadratic unbounded query work. */
    char names[16][64] = {{0}}; size_t count = 0;
    while (bytes != 0) {
        const char *amp = memchr(p, '&', bytes); size_t n = amp == NULL ? bytes : (size_t)(amp - p);
        const char *eq = memchr(p, '=', n);
        if (eq == NULL || count == 16 || !decode(p, (size_t)(eq - p), names[count], sizeof(names[0])) ||
            !decode(eq + 1, n - (size_t)(eq + 1 - p), value, sizeof(value))) { goto done; }
        for (size_t i = 0; i < count; ++i) { if (strcmp(names[i], names[count]) == 0) { goto done; } }
        const char *key = names[count++];
        if (strcmp(key, "state") == 0) {
            size_t len = strlen(value); if (len != 43) { result = OAUTH_STATE_MISMATCH; goto done; }
            memcpy(state, value, 44); have_state = true;
        } else if (strcmp(key, "code") == 0) { memcpy(code, value, strlen(value) + 1); have_code = true; }
        else if (strcmp(key, "error") == 0) { if (*value == 0) { goto done; } have_error = true; }
        SecureZeroMemory(value, sizeof(value));
        if (amp == NULL) { break; }
        size_t used = n + 1; p += used; bytes -= used;
        if (bytes == 0) { goto done; }
    }
    unsigned difference = 0;
    for (size_t i = 0; i < 43; ++i) { difference |= (unsigned char)state[i] ^ (unsigned char)f->state_key[i]; }
    if (!have_state || difference != 0) { result = OAUTH_STATE_MISMATCH; goto done; }
    if (have_code == have_error || (have_code && code[0] == 0)) { goto done; }
    if (have_error) { result = oauth_terminal(f, OAUTH_DENIED); goto done; }
    memcpy(f->code, code, sizeof(code)); f->state = OAUTH_CODE; f->result = OAUTH_OK;
    oauth_loopback_close(f); SecureZeroMemory(f->state_key, sizeof(f->state_key));
    SecureZeroMemory(f->authorization, sizeof(f->authorization)); result = OAUTH_OK;
done:
    SecureZeroMemory(code, sizeof(code)); SecureZeroMemory(state, sizeof(state)); SecureZeroMemory(value, sizeof(value)); return result;
}
OAuthResult oauth_callback_request(OAuthFlow *f, const char *request, size_t bytes)
{
    if (f == NULL || request == NULL) { return OAUTH_INVALID; }
    if (f->state != OAUTH_WAITING) { return OAUTH_REPLAY; }
    if (core_deadline_expired(core_clock_now(&f->clock), f->deadline)) { return oauth_terminal(f, OAUTH_EXPIRED); }
    if (bytes > OAUTH_REQUEST_LIMIT) { return OAUTH_FULL; }
    if (bytes < 4 || memchr(request, 0, bytes) != NULL || memcmp(request, "GET ", 4) != 0) { return OAUTH_INVALID; }
    const char *end = request + bytes;
    const char *space = memchr(request + 4, ' ', bytes - 4);
    if (space == NULL || end - space < 11 || memcmp(space, " HTTP/1.1\r\n", 11) != 0) { return OAUTH_INVALID; }
    const char *line = space + 11; bool host = false;
    while (end - line >= 2) {
        if (line[0] == '\r' && line[1] == '\n') {
            return line + 2 == end && host ? callback_target(f, request + 4, (size_t)(space - request - 4)) : OAUTH_INVALID;
        }
        const char *cr = memchr(line, '\r', (size_t)(end - line));
        if (cr == NULL || end - cr < 2 || cr[1] != '\n') { return OAUTH_INVALID; }
        const char *colon = memchr(line, ':', (size_t)(cr - line));
        if (colon == NULL) { return OAUTH_INVALID; }
        for (const char *p = line; p < cr; ++p) { if (*p < 32 || (unsigned char)*p >= 127) { return OAUTH_INVALID; } }
        const char *value = colon + 1; while (value < cr && *value == ' ') { ++value; }
        size_t key = (size_t)(colon - line), n = (size_t)(cr - value);
        if (key == 4 && _strnicmp(line, "Host", key) == 0) {
            if (host || n != strlen(f->host) || memcmp(value, f->host, n) != 0) { return OAUTH_INVALID; } host = true;
        } else if (key == 14 && _strnicmp(line, "Content-Length", key) == 0) {
            if (n != 1 || *value != '0') { return OAUTH_INVALID; }
        } else if (key == 17 && _strnicmp(line, "Transfer-Encoding", key) == 0) { return OAUTH_INVALID; }
        line = cr + 2;
    }
    return OAUTH_INVALID;
}
OAuthResult oauth_loopback_poll(OAuthFlow *f)
{
    uint64_t now = core_clock_now(&f->clock);
    if (f->peer != INVALID_SOCKET && core_deadline_expired(now, f->peer_deadline)) { peer_close(f); }
    WSANETWORKEVENTS events;
    if (WSAEnumNetworkEvents(f->listener, f->listen_event, &events) != 0) { return oauth_terminal(f, OAUTH_IO_ERROR); }
    if ((events.lNetworkEvents & FD_CLOSE) != 0) { return oauth_terminal(f, OAUTH_IO_ERROR); }
    if (f->peer == INVALID_SOCKET) {
        f->peer = accept(f->listener, NULL, NULL);
        if (f->peer == INVALID_SOCKET) { return WSAGetLastError() == WSAEWOULDBLOCK ? OAUTH_PENDING : oauth_terminal(f, OAUTH_IO_ERROR); }
        ++f->live_sockets;
        if (WSAEventSelect(f->listener, f->listen_event, 0) != 0 ||
            WSAEventSelect(f->peer, f->peer_event, FD_READ | FD_CLOSE) != 0) { return oauth_terminal(f, OAUTH_IO_ERROR); }
        f->peer_deadline = core_deadline_after(now, 5000);
    }
    if (WSAEnumNetworkEvents(f->peer, f->peer_event, &events) != 0) { return oauth_terminal(f, OAUTH_IO_ERROR); }
    while (f->request_bytes < OAUTH_REQUEST_LIMIT) {
        int n = recv(f->peer, f->request + f->request_bytes, (int)(OAUTH_REQUEST_LIMIT - f->request_bytes), 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) { return OAUTH_PENDING; }
            peer_close(f); return OAUTH_PENDING;
        }
        if (n == 0) { peer_close(f); return OAUTH_PENDING; }
        f->request_bytes += (size_t)n; f->request[f->request_bytes] = 0;
        if (strstr(f->request, "\r\n\r\n") != NULL) {
            /* Fixed response has no reflected secrets. Send before accepting
             * callback because successful validation closes the socket. */
            static const char response[] = "HTTP/1.1 200 OK\r\nContent-Length: 26\r\nConnection: close\r\nCache-Control: no-store\r\n\r\nReturn to the application.";
            (void)send(f->peer, response, (int)sizeof(response) - 1, 0);
            OAuthResult result = oauth_callback_request(f, f->request, f->request_bytes);
            if (f->state == OAUTH_WAITING) { peer_close(f); return OAUTH_PENDING; }
            return result;
        }
    }
    peer_close(f); return OAUTH_PENDING;
}
