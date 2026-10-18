#ifndef SPOTIC_RANGE_TRANSPORT_H
#define SPOTIC_RANGE_TRANSPORT_H
#include "api/http_client.h"
/* Borrowed single-owner transport. Final consumption acknowledges that the
 * producer no longer uses the request's body buffer, including on cancel. */
typedef struct RangeTransport {
    void *context;
    HttpResult (*submit)(void *, const HttpRequest *, HttpRequestId *);
    HttpResult (*pump)(void *);
    HttpResult (*consume)(void *, HttpRequestId, HttpCompletion *);
    HttpResult (*cancel)(void *, HttpRequestId);
    HANDLE (*wait_handle)(const void *);
    uint32_t (*next_wait_ms)(const void *);
} RangeTransport;
#endif
