#ifndef SPOTIC_HTTP_TEST_H
#define SPOTIC_HTTP_TEST_H
#include "support/http_server.h"
#include "api/http_internal.h"
#include "support/test_check.h"

extern Metrics http_test_metrics;
HttpClient *http_test_open(const wchar_t *origin);
void http_test_close(HttpClient *client);
void http_test_url(wchar_t *output, size_t chars, uint16_t port, const wchar_t *path);
HttpRequest http_test_request(const wchar_t *url, void *body, size_t capacity);
HttpCompletion http_test_complete(HttpClient *client, HttpRequestId id);
void test_http_lifetime(void);
void test_http_policy(void);
#endif
