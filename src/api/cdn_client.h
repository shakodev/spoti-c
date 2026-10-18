#ifndef SPOTIC_CDN_CLIENT_H
#define SPOTIC_CDN_CLIENT_H
#include "audio/range_transport.h"
typedef struct CdnClient CdnClient;
/* Fixed arena storage and one cancellable worker. Only credential-free,
 * identity-encoded bounded GET ranges are accepted. HTTP is loopback opt-in. */
HttpResult cdn_client_create(Arena *, Metrics *, bool loopback_http, CdnClient **);
const RangeTransport *cdn_client_transport(CdnClient *);
/* Timeout retains live storage. Successful shutdown joins all I/O before
 * returning, wipes signed URLs/TLS buffers and invalidates the client. */
HttpResult cdn_client_shutdown(CdnClient *, uint32_t timeout_ms);
#endif
