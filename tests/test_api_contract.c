#include "api/api_contract.h"
#include "support/test_check.h"
#include <stdint.h>
#include <string.h>

void api_contract_manifest_tests(void);
static void catalog(void)
{
    for (unsigned i = 0; i < API_OPERATION_COUNT; ++i) {
        const ApiContract *c = contract_inspect((ApiOperation)i), *out = c;
        CHECK(c != NULL && (unsigned)c->operation == i && c->name != NULL);
        if (i == API_ALBUM) {
            CHECK(contract_missing_proofs(c) == 0 && c->blocker == NULL);
            CHECK(contract_lookup((ApiOperation)i,&out) == API_CONTRACT_OK && out == c);
        } else {
            CHECK(contract_missing_proofs(c) != 0 && c->blocker != NULL);
            CHECK(contract_validate(c) == API_CONTRACT_UNAVAILABLE);
            CHECK(contract_lookup((ApiOperation)i,&out) == API_CONTRACT_UNAVAILABLE && out == NULL);
            CHECK(contract_check_body(c,false,0) == API_CONTRACT_UNAVAILABLE);
            CHECK(contract_check_body(c,true,SIZE_MAX) == API_CONTRACT_UNAVAILABLE);
        }
        for (unsigned j = 0; j < i; ++j) { CHECK(strcmp(c->name,contract_inspect((ApiOperation)j)->name) != 0); }
    }
    const ApiContract *out = contract_inspect(API_HOME);
    CHECK(contract_lookup((ApiOperation)-1,&out) == API_CONTRACT_INVALID && out == NULL);
    out = contract_inspect(API_HOME);
    CHECK(contract_lookup(API_OPERATION_COUNT,&out) == API_CONTRACT_INVALID && out == NULL);
    CHECK(contract_lookup(API_HOME,NULL) == API_CONTRACT_INVALID);
    CHECK(contract_inspect((ApiOperation)-1) == NULL && contract_inspect(API_OPERATION_COUNT) == NULL);
    CHECK(contract_validate(NULL) == API_CONTRACT_INVALID);
    CHECK(contract_check_body(NULL,false,0) == API_CONTRACT_INVALID);
    CHECK(contract_missing_proofs(NULL) == API_PROOF_ALL);
    /* Primary views must never be silently mapped to secondary documents. */
    CHECK(contract_inspect(API_PLAYLIST)->persisted_sha256 == NULL);
    CHECK(contract_inspect(API_LIBRARY)->url == NULL);
    CHECK(contract_inspect(API_LYRICS)->transport == API_HTTP_JSON);
    CHECK(strcmp(contract_inspect(API_ALBUM)->wire_operation,"getAlbum") == 0);
    CHECK(strcmp(contract_inspect(API_ARTIST)->persisted_sha256,
        "9f8134ef565e78621f1e1793555bd6633c5ac144ae0f89604ed3ae3f80b3c8e6") == 0);
}
static void proof_gate(void)
{
    /* Test-only fully populated descriptor; never enables a production entry.
     * These tests verify admission policy, not service acceptance. */
    ApiContract complete = *contract_inspect(API_ALBUM);
    complete.proofs = API_PROOF_ALL; complete.blocker = NULL;
    CHECK(contract_validate(&complete) == API_CONTRACT_OK);
    for (uint32_t bit = 1; bit <= API_PROOF_PAGINATION; bit <<= 1) {
        ApiContract c = complete; c.proofs &= ~bit; c.blocker = "test missing proof";
        CHECK(contract_missing_proofs(&c) == bit);
        CHECK(contract_validate(&c) == API_CONTRACT_UNAVAILABLE);
        c.blocker = NULL; CHECK(contract_validate(&c) == API_CONTRACT_INVALID);
    }
    CHECK(contract_check_body(&complete,false,0) == API_CONTRACT_OK);
    CHECK(contract_check_body(&complete,false,complete.request_cap) == API_CONTRACT_OK);
    CHECK(contract_check_body(&complete,false,complete.request_cap + 1) == API_CONTRACT_TOO_LARGE);
    CHECK(contract_check_body(&complete,true,complete.response_cap) == API_CONTRACT_OK);
    CHECK(contract_check_body(&complete,true,complete.response_cap + 1) == API_CONTRACT_TOO_LARGE);
    CHECK(contract_check_body(&complete,true,SIZE_MAX) == API_CONTRACT_TOO_LARGE);
#define BAD(field,value) do { ApiContract c = complete; c.field = value; \
    CHECK(contract_validate(&c) == API_CONTRACT_INVALID); } while (0)
    BAD(operation,(ApiOperation)-1); BAD(transport,(ApiTransport)-1); BAD(method,API_METHOD_UNKNOWN);
    BAD(name,""); BAD(source_file,NULL); BAD(source_sha256,"a"); BAD(source_sha256,NULL);
    BAD(evidence_id,"ev_"); BAD(evidence_id,"x"); BAD(evidence_id,NULL);
    BAD(persisted_sha256,"xyz"); BAD(persisted_sha256,NULL); BAD(wire_operation,NULL);
    BAD(url,"http://example.test/query"); BAD(url,NULL); BAD(headers,NULL); BAD(header_count,0);
    BAD(variables,NULL); BAD(variable_count,API_CONTRACT_FIELD_LIMIT + 1);
    BAD(response,NULL); BAD(response_count,0); BAD(response_count,SIZE_MAX);
    BAD(request_cap,0); BAD(request_cap,API_CONTRACT_REQUEST_CAP + 1);
    BAD(response_cap,0); BAD(response_cap,API_CONTRACT_RESPONSE_CAP + 1);
    BAD(proofs,UINT32_MAX); BAD(transport,API_NATIVE_RPC);
#undef BAD
    ApiField duplicate[] = {{"uri",API_FIELD_STRING,true,false},{"uri",API_FIELD_UINT,true,false}};
    ApiContract c = complete; c.variables = duplicate; c.variable_count = 2;
    CHECK(contract_validate(&c) == API_CONTRACT_INVALID);
    duplicate[1].name = "limit"; duplicate[1].type = (ApiFieldType)-1;
    CHECK(contract_validate(&c) == API_CONTRACT_INVALID);
    duplicate[1].type = API_FIELD_UINT; duplicate[1].name = "";
    CHECK(contract_validate(&c) == API_CONTRACT_INVALID);
    /* Corrupt already-claimed evidence must fail even on unavailable entries. */
    c = *contract_inspect(API_HOME); c.source_sha256 = "bad";
    CHECK(contract_validate(&c) == API_CONTRACT_INVALID);
}
int main(void)
{
    catalog(); proof_gate(); api_contract_manifest_tests();
    puts("API contract catalog, fail-closed proof gate and sanitized fixture manifest passed");
    return 0;
}
