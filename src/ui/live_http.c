#include "live_internal.h"
#include <string.h>
#include <wchar.h>
#include <stdio.h>

void live_notify(AppLive *h)
{
    Event e={.type=UI_DIRTY_STATE}; (void)event_post(h->events,&e);
}
bool live_wait(AppLive *h, const CancelToken *cancel, HANDLE event, DWORD ms)
{
    (void)h;
    HANDLE handles[2]={worker_cancel_handle(cancel),event};
    DWORD wait=worker_cancel_wait(cancel); if (ms<wait) { wait=ms; }
    DWORD result=WaitForMultipleObjects(event ? 2u : 1u,handles,FALSE,wait);
    return result!=WAIT_FAILED && !worker_cancelled(cancel);
}
bool live_http_open(AppLive *h)
{
    if (h->http) { return true; }
    HttpConfig c={.header_origin_count=5};
    c.header_origins[0]=L"https://accounts.spotify.com";
    c.header_origins[1]=L"https://clienttoken.spotify.com";
    c.header_origins[2]=L"https://login5.spotify.com";
    c.header_origins[3]=L"https://api-partner.spotify.com";
    c.header_origins[4]=L"https://spclient.wg.spotify.com";
    if (h->origin[0]) { c.header_origins[c.header_origin_count++]=h->origin; }
    if (!arena_reset(&h->http_arena)) { return false; }
    return http_client_create(&h->http_arena,&c,&h->http)==HTTP_OK;
}
bool live_authorize(AppLive *h, const wchar_t *url, TokenAuthorization *a, const CancelToken *cancel)
{
    if (!h->tokens) { return false; }
    for (;;) {
        TokenResult r=token_authorize(h->tokens,url,a);
        if (r==TOKEN_OK) { return true; }
        if (r!=TOKEN_PENDING || worker_cancelled(cancel)) {
            (void)snprintf(h->failure,sizeof(h->failure),"Token %u: Spotify authorization failed.",(unsigned)r); return false;
        }
        TokenResult driven=token_poll(h->tokens);
        if (driven==TOKEN_OK) { continue; }
        if (driven!=TOKEN_PENDING) {
            (void)snprintf(h->failure,sizeof(h->failure),"Token %u: Spotify token acquisition failed.",(unsigned)driven); return false;
        }
        if (!live_wait(h,cancel,token_wait_handle(h->tokens),token_next_wait_ms(h->tokens))) { return false; }
    }
}
bool live_http(AppLive *h, const wchar_t *url, HttpMethod method, const void *upload,
    size_t upload_bytes, const wchar_t *content_type, void *body, size_t cap,
    HttpCompletion *completion, const CancelToken *cancel, bool authenticate)
{
    TokenAuthorization a={0}; bool ok=false;
    h->failure[0]=0;
    h->last_http_status=0; h->last_http_bytes=0;
    memset(completion,0,sizeof(*completion));
    for (unsigned retry=0;retry<2;++retry) {
        if (worker_cancelled(cancel) || (authenticate && !live_authorize(h,url,&a,cancel))) { break; }
        bool pathfinder=wcsncmp(url,L"https://api-partner.spotify.com/",32)==0;
        HttpHeader headers[8]; size_t count=0;
        if (authenticate) {
            headers[count++]=a.headers[0];
            if (!pathfinder) { headers[count++]=a.headers[1]; }
            headers[count++]=(HttpHeader){L"App-Platform",L"Win32_x86_64"};
            headers[count++]=(HttpHeader){L"Spotify-App-Version",h->request_version ? h->request_version : pathfinder ? L"1.3.4.258" : L"130503058"};
            headers[count++]=(HttpHeader){L"Accept-Language",L"en-GB"};
        }
        headers[count++]=(HttpHeader){L"Accept",pathfinder ? L"application/json" : content_type};
        if (method==HTTP_POST) { headers[count++]=(HttpHeader){L"Content-Type",content_type}; }
        HttpRequest request={.url=url,.method=method,.headers=headers,.header_count=count,
            .upload=upload,.upload_bytes=upload_bytes,.body=body,.body_capacity=cap,.body_limit=cap,.timeout_ms=30000};
        HttpRequestId id;
        if (http_submit(h->http,&request,&id)!=HTTP_OK) { break; }
        bool cancelled=false;
        for (;;) {
            (void)http_pump(h->http);
            HttpResult r=http_consume(h->http,id,completion);
            if (r!=HTTP_PENDING) { ok=r==HTTP_OK && completion->final &&
                (completion->status!=200 || completion->result==HTTP_OK); break; }
            if (worker_cancelled(cancel) && !cancelled) { (void)http_cancel(h->http,id); cancelled=true; }
            if (cancelled) { (void)http_wait(h->http,1000); }
            else if (!live_wait(h,cancel,http_wait_handle(h->http),http_next_wait_ms(h->http))) {
                (void)http_cancel(h->http,id); cancelled=true;
            }
        }
        if (!ok || cancelled) { ok=false; break; }
        if (!authenticate || token_report_status(h->tokens,&a,completion->status)!=TOKEN_RETRY) { break; }
        ok=false;
    }
    if (!ok && !h->failure[0] && !worker_cancelled(cancel)) {
        (void)snprintf(h->failure,sizeof(h->failure),"HTTP %u/%u: Spotify request failed.",completion->status,(unsigned)completion->result);
    }
    h->last_http_status=completion->status; h->last_http_bytes=completion->bytes;
    token_authorization_clear(&a); return ok;
}
