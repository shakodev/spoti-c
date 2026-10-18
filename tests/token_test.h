#ifndef SPOTIC_TOKEN_TEST_H
#define SPOTIC_TOKEN_TEST_H
#include <winsock2.h>
#include "api/token_internal.h"
#include "support/http_server.h"
#include "support/test_check.h"
#include "auth_vectors.h"
#include <string.h>

typedef struct TokenFixture {
    Metrics metrics;
    MetricsArena secret_binding, http_binding;
    Arena secret, transport;
    HttpClient *http;
    TokenManager *manager;
    CredentialRecord record;
    TestHttpServer server;
    TestHttpResponse responses[8];
    char headers[8][256];
    wchar_t client_url[128], login_url[128], origin[128];
    TokenConfig config;
    uint64_t now, clock_step;
} TokenFixture;
extern TokenFixture token_test;
void token_test_open(const TestHttpResponse *responses, size_t count);
void token_test_close(void);
TokenResult token_test_finish(void);
void token_test_request(unsigned index, const void *body, size_t bytes, bool login);
bool token_test_zero(const void *bytes, size_t size);
void test_token_challenges(void);
void test_token_errors(void);
#define TOKEN_RESPONSE_FIXTURE(name) (TestHttpResponse){.body = auth_##name, .bytes = sizeof(auth_##name)}
#endif
