#include "cdn_internal.h"
static DWORD WINAPI transfer(void *context)
{
    CdnClient *c = context; HANDLE events[] = {c->events[CDN_STOP],c->events[CDN_WORK]};
    for (;;) {
        if (WaitForMultipleObjects(2,events,FALSE,INFINITE) != WAIT_OBJECT_0+1) { break; }
        AcquireSRWLockExclusive(&c->lock); c->state = CDN_RUNNING; ReleaseSRWLockExclusive(&c->lock);
        HttpResult result = cdn_exchange(c); cdn_connection_close(c);
        AcquireSRWLockExclusive(&c->lock);
        c->completion.result = result; c->completion.total_bytes = c->completion.bytes;
        c->state = CDN_COMPLETE; SetEvent(c->events[CDN_DONE]); ReleaseSRWLockExclusive(&c->lock);
    }
    return 0;
}
static HttpResult submit(void *context, const HttpRequest *r, HttpRequestId *id)
{
    CdnClient *c = context;
    if (!id) { return HTTP_INVALID; } *id = (HttpRequestId){0};
    if (!c || !r || r->method != HTTP_GET || !r->range || !r->identity_encoding || r->stream ||
        r->header_count || r->upload_bytes || !r->body || !r->body_capacity ||
        r->body_capacity > HTTP_BLOCK_SIZE || r->body_limit != r->body_capacity ||
        r->range_first > r->range_last || r->range_last-r->range_first >= HTTP_BLOCK_SIZE ||
        r->max_redirects || r->max_retries) { return HTTP_INVALID; }
    CdnUrl url; if (!cdn_url_parse(r->url,c->loopback,&url)) { return HTTP_DESTINATION; }
    AcquireSRWLockExclusive(&c->lock); HttpResult result = HTTP_OK;
    if (c->closing || c->closed) { result = HTTP_CLOSED; }
    else if (c->state != CDN_IDLE) { result = HTTP_FULL; }
    else if (c->generation == UINT32_MAX) { result = HTTP_EXHAUSTED; }
    else {
        *id = (HttpRequestId){0,++c->generation}; c->url = url;
        c->body = r->body; c->capacity = r->body_capacity; c->first = r->range_first; c->last = r->range_last;
        uint32_t timeout = r->timeout_ms ? r->timeout_ms : 30000; if (timeout > 60000) { timeout = 60000; }
        c->deadline = GetTickCount64()+timeout;
        c->completion = (HttpCompletion){.id = *id, .result = HTTP_PENDING, .attempts = 1,
            .body = r->body, .final = true, .headers = c->headers}; c->headers[0] = 0;
        ResetEvent(c->events[CDN_CANCEL]); ResetEvent(c->events[CDN_DONE]);
        c->state = CDN_QUEUED; SetEvent(c->events[CDN_WORK]);
    }
    ReleaseSRWLockExclusive(&c->lock); return result;
}
static HttpResult pump(void *context) { return context ? HTTP_OK : HTTP_INVALID; }
static HttpResult consume(void *context, HttpRequestId id, HttpCompletion *out)
{
    CdnClient *c = context; if (!c || !out) { return HTTP_INVALID; }
    AcquireSRWLockExclusive(&c->lock); HttpResult result = HTTP_PENDING;
    if (!id.generation || id.generation != c->generation || id.slot || c->state == CDN_IDLE) { result = HTTP_STALE; }
    else if (c->state == CDN_COMPLETE) {
        *out = c->completion; c->state = CDN_IDLE; ResetEvent(c->events[CDN_DONE]); result = HTTP_OK;
    }
    ReleaseSRWLockExclusive(&c->lock); return result;
}
static HttpResult cancel(void *context, HttpRequestId id)
{
    CdnClient *c = context; if (!c) { return HTTP_INVALID; }
    AcquireSRWLockExclusive(&c->lock); HttpResult result = HTTP_OK;
    if (!id.generation || id.generation != c->generation || id.slot || c->state == CDN_IDLE) { result = HTTP_STALE; }
    else if (c->state != CDN_COMPLETE) { SetEvent(c->events[CDN_CANCEL]); }
    ReleaseSRWLockExclusive(&c->lock); return result;
}
static HANDLE event(const void *context) { return ((const CdnClient *)context)->events[CDN_DONE]; }
static uint32_t wait_ms(const void *context) { (void)context; return INFINITE; }
HttpResult cdn_client_create(Arena *arena, Metrics *metrics, bool loopback, CdnClient **out)
{
    if (!out) { return HTTP_INVALID; } *out = NULL;
    if (!arena) { return HTTP_INVALID; } ArenaMark mark = arena_mark(arena);
    CdnClient *c = arena_alloc(arena,sizeof(*c),_Alignof(CdnClient)); if (!c) { return HTTP_NO_MEMORY; }
    memset(c,0,sizeof(*c)); c->socket = INVALID_SOCKET; c->metrics = metrics; c->loopback = loopback;
    InitializeSRWLock(&c->lock); c->transport = (RangeTransport){c,submit,pump,consume,cancel,event,wait_ms};
    WSADATA data; bool ok = WSAStartup(MAKEWORD(2,2),&data) == 0; c->winsock = ok;
    for (unsigned i = 0; ok && i < CDN_THREAD; ++i) {
        c->events[i] = CreateEventW(NULL,i != CDN_WORK,FALSE,NULL);
        ok = c->events[i] && cdn_track(c,i,(uintptr_t)c->events[i]);
    }
    if (ok) {
        c->events[CDN_THREAD] = CreateThread(NULL,0,transfer,c,0,NULL);
        ok = c->events[CDN_THREAD] && cdn_track(c,CDN_THREAD,(uintptr_t)c->events[CDN_THREAD]);
    }
    if (!ok) {
        (void)cdn_client_shutdown(c,INFINITE); (void)arena_rewind(arena,mark); return HTTP_OS_ERROR;
    }
    *out = c; return HTTP_OK;
}
const RangeTransport *cdn_client_transport(CdnClient *c) { return c ? &c->transport : NULL; }
HttpResult cdn_client_shutdown(CdnClient *c, uint32_t timeout)
{
    if (!c || c->closed) { return HTTP_INVALID; }
    AcquireSRWLockExclusive(&c->lock); c->closing = true;
    if (c->events[CDN_CANCEL]) { SetEvent(c->events[CDN_CANCEL]); }
    if (c->events[CDN_STOP]) { SetEvent(c->events[CDN_STOP]); }
    ReleaseSRWLockExclusive(&c->lock);
    if (c->events[CDN_THREAD] && WaitForSingleObject(c->events[CDN_THREAD],timeout) != WAIT_OBJECT_0) { return HTTP_TIMEOUT; }
    bool ok = !c->credential_ready && !c->context_ready && c->socket == INVALID_SOCKET;
    for (unsigned i = 0; i < CDN_EVENTS; ++i) {
        if (c->events[i]) {
            if (CloseHandle(c->events[i])) { cdn_untrack(c,i); } else { ok = false; }
        }
    }
    if (c->winsock) { ok &= WSACleanup() == 0; }
    SecureZeroMemory(c,sizeof(*c)); c->closed = true; return ok ? HTTP_OK : HTTP_OS_ERROR;
}
