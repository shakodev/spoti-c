#include "cdn_internal.h"
#include <stdio.h>
static HttpResult read_body(CdnClient *c)
{
    size_t header = 0, expected = 0; bool ready = false;
    for (;;) {
        size_t bytes;
        HttpResult r = c->url.tls ? cdn_tls_read(c,c->input,sizeof(c->input),&bytes) :
            cdn_socket_read(c,c->input,sizeof(c->input),&bytes);
        if (r != HTTP_OK) { return r; }
        if (!bytes) { return HTTP_TRUNCATED; }
        size_t at = 0;
        if (!ready) {
            while (at < bytes) {
                if (header == HTTP_HEADER_LIMIT) { return HTTP_HEADERS_TOO_LARGE; }
                c->header[header++] = (char)c->input[at++];
                if (header >= 4 && !memcmp(c->header+header-4,"\r\n\r\n",4)) {
                    c->header[header] = 0;
                    r = cdn_headers_parse(c->header,header,c->capacity,&c->completion.status,&expected,
                        c->headers,sizeof(c->headers)/sizeof(c->headers[0]));
                    if (r != HTTP_OK) { return r; } ready = true; break;
                }
            }
        }
        if (ready) {
            size_t n = bytes-at;
            if (n > expected-c->completion.bytes) { return HTTP_BODY_TOO_LARGE; }
            memcpy((uint8_t *)c->body+c->completion.bytes,c->input+at,n); c->completion.bytes += n;
            if (c->completion.bytes == expected) { return HTTP_OK; }
        }
    }
}
HttpResult cdn_exchange(CdnClient *c)
{
    HttpResult result = cdn_socket_connect(c);
    if (result != HTTP_OK) { return result; }
    if (c->url.tls) { result = cdn_tls_open(c); if (result != HTTP_OK) { return result; } }
    int bytes = sprintf_s(c->request,sizeof(c->request),"GET %s HTTP/1.1\r\nHost: %s\r\n"
        "Range: bytes=%llu-%llu\r\nAccept-Encoding: identity\r\nConnection: close\r\nUser-Agent: spoti-c/0.1\r\n\r\n",
        c->url.path,c->url.authority,(unsigned long long)c->first,(unsigned long long)c->last);
    if (bytes <= 0) { return HTTP_INVALID; }
    result = c->url.tls ? cdn_tls_send(c,c->request,(size_t)bytes) : cdn_socket_send(c,c->request,(size_t)bytes);
    return result == HTTP_OK ? read_body(c) : result;
}
