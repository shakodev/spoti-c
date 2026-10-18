#ifndef SPOTIC_CDN_INTERNAL_H
#define SPOTIC_CDN_INTERNAL_H
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include "cdn_client.h"
#include <string.h>
enum { CDN_TLS_BYTES = 32768, CDN_TLS_WRITE = 18432, CDN_INPUT_BYTES = 8192 };
enum { CDN_WORK, CDN_DONE, CDN_CANCEL, CDN_STOP, CDN_SOCKET, CDN_DNS, CDN_THREAD, CDN_EVENTS };
enum { CDN_IDLE, CDN_QUEUED, CDN_RUNNING, CDN_COMPLETE };
typedef struct CdnUrl {
    wchar_t host[256];
    char authority[300], path[HTTP_URL_CHARS];
    uint16_t port;
    bool tls;
} CdnUrl;
struct CdnClient {
    RangeTransport transport;
    SRWLOCK lock;
    Metrics *metrics;
    HANDLE events[CDN_EVENTS];
    MetricsHandle tracked[CDN_EVENTS + 1];
    SOCKET socket;
    unsigned state;
    uint32_t generation;
    bool loopback, winsock, closing, closed, credential_ready, context_ready;
    CdnUrl url;
    uint64_t first, last, deadline;
    void *body;
    size_t capacity;
    HttpCompletion completion;
    CredHandle credential;
    CtxtHandle context;
    SECURITY_STATUS tls_status; /* Last SSPI status; no peer/account data. */
    SecPkgContext_StreamSizes sizes;
    size_t encrypted, plain_at, plain_bytes, extra_at, extra_bytes;
    char request[HTTP_URL_CHARS + 700];
    char header[HTTP_HEADER_LIMIT + 1];
    wchar_t headers[HTTP_HEADER_LIMIT + 1];
    uint8_t input[CDN_INPUT_BYTES], tls_input[CDN_TLS_BYTES], tls_output[CDN_TLS_WRITE];
};
_Static_assert(sizeof(CdnClient) < 128 * 1024, "bounded native CDN storage");
bool cdn_url_parse(const wchar_t *, bool loopback, CdnUrl *);
HttpResult cdn_headers_parse(const char *, size_t, size_t, unsigned *, size_t *, wchar_t *, size_t);
HttpResult cdn_socket_connect(CdnClient *);
HttpResult cdn_socket_send(CdnClient *, const void *, size_t);
HttpResult cdn_socket_read(CdnClient *, void *, size_t, size_t *);
void cdn_connection_close(CdnClient *);
HttpResult cdn_tls_open(CdnClient *);
HttpResult cdn_tls_send(CdnClient *, const void *, size_t);
HttpResult cdn_tls_read(CdnClient *, void *, size_t, size_t *);
HttpResult cdn_exchange(CdnClient *);
bool cdn_track(CdnClient *, unsigned, uintptr_t);
void cdn_untrack(CdnClient *, unsigned);
uint32_t cdn_remaining(const CdnClient *);
HttpResult cdn_wait(CdnClient *, HANDLE);
#endif
