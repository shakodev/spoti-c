#include "api/cdn_internal.h"
#include "api/http_internal.h"
#include "headless/fixture_server.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include <stdio.h>
#include <wchar.h>
static _Alignas(16) uint8_t memory[MEMORY_BUDGET_HTTP];
static uint8_t body[65536], golden[65536];
_Static_assert(sizeof(HttpClient)+sizeof(CdnClient)<=MEMORY_BUDGET_HTTP,"control/CDN clients share the fixed HTTP arena");
void cdn_tls_tests(void);
static void urls(void)
{
    CdnUrl u;
    CHECK(cdn_url_parse(L"https://cdn.example/audio?token=public",false,&u));
    CHECK(u.tls && u.port == 443 && !strcmp(u.path,"/audio?token=public"));
    CHECK(cdn_url_parse(L"https://[::1]:8443/?a=1",false,&u) && u.port == 8443);
    CHECK(cdn_url_parse(L"http://127.0.0.1:1234/audio",true,&u) && !u.tls);
    CHECK(!cdn_url_parse(L"http://127.0.0.1/audio",false,&u));
    const wchar_t *bad[] = {L"https://user@host/x",L"http://example.com/x",L"https://host:0/x",
        L"https://host:65536/x",L"https://host:/x",L"https://[bogus]/x",L"https:///x",
        L"https://host/x#fragment",L"https://host/x\r\nCookie: secret",L"https://host\\x"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { CHECK(!cdn_url_parse(bad[i],true,&u)); }
}
static void headers(void)
{
    wchar_t wide[8193]; unsigned status=0; size_t bytes=0;
    const char *ok="HTTP/1.1 206 Partial Content\r\nContent-Length: 4\r\nContent-Range: bytes 0-3/4\r\n\r\n";
    CHECK(cdn_headers_parse(ok,strlen(ok),4,&status,&bytes,wide,8193)==HTTP_OK && status==206 && bytes==4);
    CHECK(cdn_headers_parse(ok,strlen(ok),3,&status,&bytes,wide,8193)==HTTP_BODY_TOO_LARGE);
    const char *bad[] = {
        "HTTP/1.1 206 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\n",
        "HTTP/1.1 206 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 206 OK\r\nContent-Length: 184467440737095516160\r\n\r\n",
        "HTTP/1.1 206 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 206 OK\r\nContent-Length: 1\r\nContent-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 206 OK\r\nContent-Length: 1\r\n folded: no\r\n\r\n",
        "HTTP/1.1 206 OK\r\nContent-Length: 1\r\nBad Name: value\r\n\r\n",
        "HTTP/1.1 206 OK\nBad: value\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 206 OK\r\n\r\n", "HTTP/2.0 206 OK\r\nContent-Length: 0\r\n\r\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { CHECK(cdn_headers_parse(bad[i],strlen(bad[i]),65536,&status,&bytes,wide,8193)!=HTTP_OK); }
    for(size_t n=0;n<strlen(ok);++n) { CHECK(cdn_headers_parse(ok,n,65536,&status,&bytes,wide,8193)!=HTTP_OK); }
    char large[8193]; memset(large,'x',sizeof(large));
    CHECK(cdn_headers_parse(large,sizeof(large),65536,&status,&bytes,wide,8193)==HTTP_HEADERS_TOO_LARGE);
}
static HttpCompletion completed(const RangeTransport *t, HttpRequestId id)
{
    HttpCompletion result; uint64_t until=GetTickCount64()+6000;
    for(;;) {
        HttpResult r=t->consume(t->context,id,&result);
        if(r==HTTP_OK) { return result; } CHECK(r==HTTP_PENDING && GetTickCount64()<until);
        CHECK(WaitForSingleObject(t->wait_handle(t->context),1000)!=WAIT_FAILED);
    }
}
static void transfers(void)
{
    HeadlessServer server; CHECK(headless_server_open(&server,L"" HEADLESS_FIXTURE_DIR L"/synthetic.enc"));
    FILE *file=NULL; CHECK(fopen_s(&file,HEADLESS_FIXTURE_DIR "/synthetic.enc","rb")==0);
    CHECK(fread(golden,1,sizeof(golden),file)==sizeof(golden)); CHECK(fclose(file)==0);
    wchar_t url[128]; CHECK(swprintf_s(url,128,L"http://127.0.0.1:%u/audio",(unsigned)server.port)>0);
    HttpRequest request={.url=url,.method=HTTP_GET,.range=true,.identity_encoding=true,
        .range_last=65535,.body=body,.body_capacity=sizeof(body),.body_limit=sizeof(body),.timeout_ms=2000};
    Metrics ledger; CHECK(metrics_init(&ledger)); Arena a; MetricsArena binding={0}; CdnClient *client;
    CHECK(arena_init(&a,memory,sizeof(memory)) && metrics_attach_arena(&ledger,&a,&binding));
    CHECK(cdn_client_create(&a,&ledger,true,&client)==HTTP_OK);
    const RangeTransport *t=cdn_client_transport(client); HttpRequestId id;
    for(unsigned i=0;i<8;++i) {
        CHECK(t->submit(t->context,&request,&id)==HTTP_OK);
        HttpRequestId extra; CHECK(t->submit(t->context,&request,&extra)==HTTP_FULL);
        HttpCompletion c=completed(t,id);
        CHECK(c.final && c.result==HTTP_OK && c.status==206 && c.bytes==sizeof(body));
        CHECK(!memcmp(body,golden,sizeof(body)) && wcsstr(c.headers,L"Content-Range: bytes 0-65535/"));
        CHECK(t->consume(t->context,id,&c)==HTTP_STALE && t->cancel(t->context,id)==HTTP_STALE);
    }
    request.header_count=1; CHECK(t->submit(t->context,&request,&id)==HTTP_INVALID); request.header_count=0;
    request.url=L"http://example.com/audio"; CHECK(t->submit(t->context,&request,&id)==HTTP_DESTINATION); request.url=url;
    for(unsigned i=0;i<8;++i) {
        CHECK(t->submit(t->context,&request,&id)==HTTP_OK && t->cancel(t->context,id)==HTTP_OK);
        HttpCompletion c=completed(t,id); CHECK(c.result==HTTP_CANCELLED || c.result==HTTP_OK);
    }
    atomic_store(&server.fault,4);
    CHECK(t->submit(t->context,&request,&id)==HTTP_OK);
    Sleep(20); CHECK(t->cancel(t->context,id)==HTTP_OK); CHECK(completed(t,id).result==HTTP_CANCELLED);
    CHECK(cdn_client_shutdown(client,5000)==HTTP_OK); arena_dispose(&a); CHECK(metrics_balanced(&ledger));
    CHECK(headless_server_close(&server));
}
static void lifetimes(void)
{
    DWORD before=0,after=0;
    for(unsigned i=0;i<17;++i) {
        Arena a; CdnClient *client; CHECK(arena_init(&a,memory,sizeof(memory)));
        if(i==1) { CHECK(GetProcessHandleCount(GetCurrentProcess(),&before)); }
        CHECK(cdn_client_create(&a,NULL,false,&client)==HTTP_OK);
        CHECK(cdn_client_shutdown(client,5000)==HTTP_OK); arena_dispose(&a);
    }
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    Arena tiny; CdnClient *out=(CdnClient *)(uintptr_t)1; CHECK(arena_init(&tiny,memory,1));
    CHECK(cdn_client_create(&tiny,NULL,false,&out)==HTTP_NO_MEMORY && !out); arena_dispose(&tiny);
}
int main(void)
{
    urls(); headers(); transfers(); lifetimes(); cdn_tls_tests();
    puts("Native CDN URL/framing, exact ciphertext, cancellation and resource lifetime passed."); return 0;
}
