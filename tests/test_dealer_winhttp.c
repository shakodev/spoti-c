#include <winsock2.h>
#include "protocol/dealer_internal.h"
#include "protocol/crypto.h"
#include "support/test_check.h"
#include <string.h>

typedef struct Peer { SOCKET listener; HANDLE thread, stop; uint16_t port; } Peer;
static void base64(const unsigned char digest[20], char out[29])
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned at = 0;
    for (unsigned i = 0; i < 20; i += 3) {
        unsigned a = digest[i], b = i + 1 < 20 ? digest[i + 1] : 0, c = i + 2 < 20 ? digest[i + 2] : 0;
        out[at++] = table[a >> 2]; out[at++] = table[((a & 3) << 4) | b >> 4];
        out[at++] = i + 1 < 20 ? table[((b & 15) << 2) | c >> 6] : '=';
        out[at++] = i + 2 < 20 ? table[c & 63] : '=';
    }
    out[at] = 0;
}
static bool bytes(SOCKET socket, void *out, int count)
{
    unsigned char *p = out;
    while (count) { int n = recv(socket, (char *)p, count, 0); if (n <= 0) { return false; } p += n; count -= n; }
    return true;
}
static DWORD WINAPI serve(void *context)
{
    Peer *p = context; SOCKET socket = accept(p->listener, NULL, NULL); CHECK(socket != INVALID_SOCKET);
    DWORD timeout = 5000; CHECK(!setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)));
    char request[8192] = {0}; size_t used = 0;
    while (!strstr(request, "\r\n\r\n")) {
        CHECK(used + 1 < sizeof(request)); CHECK(bytes(socket, request + used, 1)); request[++used] = 0;
    }
    char *key = strstr(request, "Sec-WebSocket-Key: "); CHECK(key); key += strlen("Sec-WebSocket-Key: ");
    CHECK(strstr(key, "\r\n") == key + 24);
    char input[61]; memcpy(input, key, 24); memcpy(input + 24, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
    Crypto crypto; crypto_init(&crypto); unsigned char digest[20];
    CHECK(crypto_sha1(&crypto, input, 60, digest) == CRYPTO_OK && crypto_balanced(&crypto));
    char accept_value[29], response[256]; base64(digest, accept_value);
    int count = sprintf_s(response, sizeof(response), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_value);
    CHECK(count > 0 && send(socket, response, count, 0) == count);
    const unsigned char frames[] = {0x01,3,'h','e','l',0x89,1,'x',0x80,2,'l','o'};
    CHECK(send(socket, (const char *)frames, sizeof(frames), 0) == sizeof(frames));
    bool ack = false;
    while (!ack) {
        unsigned char header[2], mask[4], payload[125]; CHECK(bytes(socket, header, 2));
        CHECK(header[1] & 0x80); unsigned n = header[1] & 127; CHECK(n <= 125 && bytes(socket, mask, 4) && bytes(socket, payload, (int)n));
        for (unsigned i = 0; i < n; ++i) { payload[i] ^= mask[i % 4]; }
        if ((header[0] & 15) == 1) { CHECK(n == 3 && !memcmp(payload, "ack", 3)); ack = true; }
        else { CHECK((header[0] & 15) == 10); }
    }
    CHECK(WaitForSingleObject(p->stop, 5000) == WAIT_OBJECT_0);
    CHECK(closesocket(socket) == 0); return 0;
}
void dealer_winhttp_tests(void)
{
    WSADATA data; CHECK(!WSAStartup(MAKEWORD(2,2), &data)); Peer peer = {0};
    peer.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); CHECK(peer.listener != INVALID_SOCKET);
    struct sockaddr_in address = {.sin_family = AF_INET}; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(!bind(peer.listener, (struct sockaddr *)&address, sizeof(address))); int size = sizeof(address);
    CHECK(!getsockname(peer.listener, (struct sockaddr *)&address, &size) && !listen(peer.listener, 1)); peer.port = ntohs(address.sin_port);
    peer.stop = CreateEventW(NULL, TRUE, FALSE, NULL); CHECK(peer.stop);
    peer.thread = CreateThread(NULL, 0, serve, &peer, 0, NULL); CHECK(peer.thread);
    DealerNative native = {.fixture_loopback = true}; const DealerTransport *t = &dealer_native_transport;
    ApEndpoint endpoint = {.host = "127.0.0.1", .port = peer.port};
    CHECK(t->open(&native, &endpoint, L"/fixture") == DEALER_PENDING);
    uint64_t until = GetTickCount64() + 10000; char message[16]; size_t length = 0; bool ready = false, complete = false;
    while (!complete) {
        CHECK(GetTickCount64() < until); DealerFrame frame; DealerResult r = t->poll(&native, &frame);
        CHECK(r == DEALER_OK || r == DEALER_PENDING);
        if (r == DEALER_OK) {
            if (frame.type == DEALER_READY) { ready = true; }
            else {
                CHECK(ready && (frame.type == DEALER_TEXT_PART || frame.type == DEALER_TEXT_END));
                CHECK(length + frame.bytes <= sizeof(message)); memcpy(message + length, frame.data, frame.bytes); length += frame.bytes;
                complete = frame.type == DEALER_TEXT_END;
            }
        } else { CHECK(WaitForSingleObject(t->event(&native), 100) != WAIT_FAILED); }
    }
    CHECK(length == 5 && !memcmp(message, "hello", 5)); DWORD interval = 14999;
    CHECK(!WinHttpSetOption(native.socket, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &interval, sizeof(interval)) && GetLastError() == ERROR_INVALID_PARAMETER);
    interval = 30000; CHECK(WinHttpSetOption(native.socket, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &interval, sizeof(interval)));
    CHECK(t->send(&native, "ack", 3) == DEALER_OK); bool written = false;
    while (!written) {
        CHECK(GetTickCount64() < until); DealerFrame frame; DealerResult r = t->poll(&native, &frame);
        CHECK(r == DEALER_PENDING || (r == DEALER_OK && frame.type == DEALER_WRITTEN)); written = r == DEALER_OK;
        if (!written) { (void)WaitForSingleObject(t->event(&native), 100); }
    }
    DealerFrame frame; CHECK(t->poll(&native, &frame) == DEALER_PENDING);
    DealerResult closed;
    while ((closed = t->close(&native)) == DEALER_PENDING) { CHECK(GetTickCount64() < until); (void)WaitForSingleObject(t->event(&native), 100); }
    CHECK(closed == DEALER_OK && !native.handles && !native.event && !native.session && !native.connection && !native.socket && !native.request);
    SetEvent(peer.stop); CHECK(WaitForSingleObject(peer.thread, 5000) == WAIT_OBJECT_0);
    CHECK(CloseHandle(peer.thread) && CloseHandle(peer.stop) && !closesocket(peer.listener) && !WSACleanup());
}
