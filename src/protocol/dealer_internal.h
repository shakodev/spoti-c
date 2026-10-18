#ifndef SPOTIC_DEALER_INTERNAL_H
#define SPOTIC_DEALER_INTERNAL_H
#include "dealer.h"
#include <winhttp.h>

typedef struct DealerNative {
    HINTERNET session, connection, request, socket;
    HANDLE event; SRWLOCK lock;
    unsigned handles, flags, bytes;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
    DWORD error;
    bool opening, read_pending, send_pending, need_read, closing;
    bool fixture_loopback; /* Internal test seam; never enabled by DealerConfig. */
    unsigned char receive[8192], send[2048];
} DealerNative;
struct DealerClient {
    DealerConfig config; DealerSnapshot view;
    unsigned char *wire, *scratch;
    tinfl_decompressor inflater;
    DealerNative native;
    TokenAuthorization authorization;
    uint64_t deadline, expires;
    bool stopping, opened, writing;
    unsigned unauthorized;
    wchar_t origin[300], path[4096];
};
extern const DealerTransport dealer_native_transport;
DealerResult dealer_credentials(DealerClient *);
void dealer_disconnect(DealerClient *, DealerResult);
#endif
