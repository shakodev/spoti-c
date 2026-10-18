#include "range_internal.h"
HttpResult range_transfer_submit(RangeFetcher *f, const HttpRequest *r, HttpRequestId *id)
{
    const RangeTransport *t = f->config.transport;
    return t ? t->submit(t->context,r,id) : http_submit(f->config.http,r,id);
}
HttpResult range_transfer_pump(RangeFetcher *f)
{
    const RangeTransport *t = f->config.transport;
    return t ? t->pump(t->context) : http_pump(f->config.http);
}
HttpResult range_transfer_consume(RangeFetcher *f, HttpRequestId id, HttpCompletion *out)
{
    const RangeTransport *t = f->config.transport;
    return t ? t->consume(t->context,id,out) : http_consume(f->config.http,id,out);
}
HttpResult range_transfer_cancel(RangeFetcher *f, HttpRequestId id)
{
    const RangeTransport *t = f->config.transport;
    return t ? t->cancel(t->context,id) : http_cancel(f->config.http,id);
}
HANDLE range_transfer_event(const RangeFetcher *f)
{
    const RangeTransport *t = f->config.transport;
    return t ? t->wait_handle(t->context) : http_wait_handle(f->config.http);
}
uint32_t range_transfer_wait(const RangeFetcher *f)
{
    const RangeTransport *t = f->config.transport;
    return t ? t->next_wait_ms(t->context) : http_next_wait_ms(f->config.http);
}
