#include "api/cdn_internal.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include <stdio.h>
#include <wchar.h>
void cdn_tls_tests(void)
{
    /* Independent local TLS oracle. It reads only the PUBLIC synthetic key;
     * neither process changes Windows certificate stores or trust policy. */
    SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE}; HANDLE read,write;
    CHECK(CreatePipe(&read,&write,&security,0) && SetHandleInformation(read,HANDLE_FLAG_INHERIT,0));
    STARTUPINFOW startup={0}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdOutput=write; startup.hStdError=write; startup.hStdInput=NULL;
    wchar_t command[1024]; CHECK(swprintf_s(command,1024,L"\"%ls\" -u \"%ls\"",L"" CDN_PYTHON,L"" CDN_SERVER_SCRIPT)>0);
    PROCESS_INFORMATION process={0};
    CHECK(CreateProcessW(NULL,command,NULL,NULL,TRUE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process));
    CHECK(CloseHandle(write)); unsigned port=0; DWORD got; char ch;
    for(unsigned i=0;;++i) {
        CHECK(i<8 && ReadFile(read,&ch,1,&got,NULL) && got==1);
        if(ch=='\n') { break; }
        if(ch=='\r') { continue; }
        CHECK(ch>='0' && ch<='9'); port=port*10+(unsigned)(ch-'0');
    }
    CHECK(CloseHandle(read) && port>0 && port<65536);
    static _Alignas(16) uint8_t memory[MEMORY_BUDGET_HTTP]; Arena arena; Metrics metrics; MetricsArena binding={0};
    CHECK(metrics_init(&metrics) && arena_init(&arena,memory,sizeof(memory)) && metrics_attach_arena(&metrics,&arena,&binding));
    CdnClient *client; CHECK(cdn_client_create(&arena,&metrics,false,&client)==HTTP_OK);
    wchar_t url[128]; CHECK(swprintf_s(url,128,L"https://127.0.0.1:%u/audio",port)>0);
    uint8_t body=0x5a; HttpRequest request={.url=url,.method=HTTP_GET,.range=true,.identity_encoding=true,
        .body=&body,.body_capacity=1,.body_limit=1,.timeout_ms=4000}; HttpRequestId id;
    const RangeTransport *t=cdn_client_transport(client); CHECK(t->submit(t->context,&request,&id)==HTTP_OK);
    CHECK(WaitForSingleObject(t->wait_handle(t->context),10000)==WAIT_OBJECT_0);
    HttpCompletion completion; CHECK(t->consume(t->context,id,&completion)==HTTP_OK);
    if(client->tls_status!=SEC_E_UNTRUSTED_ROOT) { fprintf(stderr,"TLS fixture rejection status=%lx\n",(unsigned long)client->tls_status); }
    CHECK(client->tls_status==SEC_E_UNTRUSTED_ROOT && completion.result==HTTP_OS_ERROR && !completion.bytes && body==0x5a);
    CHECK(!client->credential_ready && !client->context_ready && client->socket==INVALID_SOCKET);
    CHECK(cdn_client_shutdown(client,5000)==HTTP_OK); arena_dispose(&arena); CHECK(metrics_balanced(&metrics));
    CHECK(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0);
    DWORD exit_code; CHECK(GetExitCodeProcess(process.hProcess,&exit_code) && exit_code==0);
    CHECK(CloseHandle(process.hProcess) && CloseHandle(process.hThread));
}
