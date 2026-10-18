#include "api/cdn_client.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include <stdio.h>
static _Alignas(16) uint8_t memory[MEMORY_BUDGET_HTTP], body[49152];
int main(void)
{
    /* Explicit account-free network opt-in, absent from default offline CTest.
     * Multiple TLS records exercise both buffered plaintext and extra records. */
    Arena arena; Metrics metrics; MetricsArena binding={0}; CdnClient *client;
    CHECK(metrics_init(&metrics) && arena_init(&arena,memory,sizeof(memory)) && metrics_attach_arena(&metrics,&arena,&binding));
    CHECK(cdn_client_create(&arena,&metrics,false,&client)==HTTP_OK);
    HttpRequest request={.url=L"https://httpbin.org/range/65536",.method=HTTP_GET,.range=true,.identity_encoding=true,
        .range_first=10000,.range_last=10000+sizeof(body)-1,.body=body,
        .body_capacity=sizeof(body),.body_limit=sizeof(body),.timeout_ms=15000};
    const RangeTransport *t=cdn_client_transport(client); HttpRequestId id;
    CHECK(t->submit(t->context,&request,&id)==HTTP_OK);
    CHECK(WaitForSingleObject(t->wait_handle(t->context),20000)==WAIT_OBJECT_0);
    HttpCompletion completion; CHECK(t->consume(t->context,id,&completion)==HTTP_OK);
    bool received=completion.final && completion.result==HTTP_OK && completion.status==206 && completion.bytes==sizeof(body);
    if(!received) { fprintf(stderr,"CDN HTTPS result=%u status=%u bytes=%zu\n",(unsigned)completion.result,completion.status,completion.bytes); }
    CHECK(cdn_client_shutdown(client,5000)==HTTP_OK); arena_dispose(&arena); CHECK(metrics_balanced(&metrics));
    CHECK(received); puts("Native DNS, trusted TLS and 49,152-byte HTTPS range passed; no account used."); return 0;
}
