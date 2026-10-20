#include "support/http_server.h"
#include "ui/live_internal.h"
#include "support/test_check.h"
#include <string.h>

int main(void)
{
    Metrics metrics; CHECK(metrics_init(&metrics));
    static const char resolver_body[]="{\"accesspoint\":[\"ap.spotify.com:443\"],\"dealer\":[\"dealer.spotify.com:443\"],\"spclient\":[\"spclient.spotify.com:443\"]}";
    char resolver_headers[160]; CHECK(sprintf_s(resolver_headers,sizeof(resolver_headers),"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",sizeof(resolver_body)-1)>0);
    const TestHttpResponse responses[]={
        {.headers="HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\n",.body="hey",.bytes=3},
        {.headers="HTTP/1.1 200 OK\r\nContent-Length: 3\r\nConnection: close\r\n\r\n",.body="hey",.bytes=3},
        {.headers=resolver_headers,.body=resolver_body,.bytes=sizeof(resolver_body)-1}
    };
    TestHttpServer server; CHECK(test_http_server_start(&server,&metrics,responses,3));
    AppLive *h=VirtualAlloc(NULL,sizeof(*h),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(h);
    CHECK(arena_init(&h->http_arena,h->http_storage,sizeof(h->http_storage)));
    wchar_t origin[80], url[100]; CHECK(swprintf_s(origin,80,L"http://127.0.0.1:%u",server.port)>0);
    CHECK(swprintf_s(url,100,L"%ls/data",origin)>0);
    HttpConfig config={.allow_loopback_http=true,.header_origins={origin},.header_origin_count=1};
    CHECK(http_client_create(&h->http_arena,&config,&h->http)==HTTP_OK);
    CancelToken cancel={.deadline=CORE_CLOCK_NEVER}; atomic_init(&cancel.requested,false);
    cancel.wake=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(cancel.wake);
    char body[16]={0}; HttpCompletion c;
    CHECK(live_http(h,url,HTTP_GET,NULL,0,L"application/json",body,sizeof(body),&c,&cancel,false));
    CHECK(c.status==200 && c.bytes==3 && !memcmp(body,"hey",3));
    CHECK(!strstr(server.requests[0],"Authorization:") && !strstr(server.requests[0],"client-token:"));
    CHECK(!live_http(h,url,HTTP_GET,NULL,0,L"application/json",body,2,&c,&cancel,false));
    h->resolver_url=url; ApEndpoints endpoints;
    bool resolved=live_resolve(h,&endpoints,&cancel); if (!resolved) { fprintf(stderr,"%s\n",h->failure); }
    CHECK(resolved); CHECK(endpoints.spclient_count==1 && endpoints.spclient[0].port==443);
    atomic_store_explicit(&cancel.requested,true,memory_order_release);
    CHECK(!live_http(h,url,HTTP_GET,NULL,0,L"application/json",body,sizeof(body),&c,&cancel,false));
    CHECK(http_client_shutdown(h->http,10000)==HTTP_OK); CHECK(CloseHandle(cancel.wake));
    arena_dispose(&h->http_arena); CHECK(VirtualFree(h,0,MEM_RELEASE));
    test_http_server_stop(&server); CHECK(metrics_balanced(&metrics));
    puts("Live transport completion, body limits, cancellation and retirement passed."); return 0;
}
