#ifndef SPOTIC_HEADLESS_FIXTURE_SERVER_H
#define SPOTIC_HEADLESS_FIXTURE_SERVER_H
#include <winsock2.h>
#include <windows.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef struct HeadlessServer {
    SOCKET listener, peer;
    HANDLE stop, network, thread, file;
    uint64_t bytes;
    _Atomic uint64_t requests;
    _Atomic unsigned fault; /* 1: wrong range; 2: truncated response; 3: ciphertext corruption. */
    bool winsock;
    unsigned short port;
} HeadlessServer;
bool headless_server_open(HeadlessServer *, const wchar_t *encrypted_file);
bool headless_server_close(HeadlessServer *);
#endif
