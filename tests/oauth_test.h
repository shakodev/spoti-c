#ifndef SPOTIC_OAUTH_TEST_H
#define SPOTIC_OAUTH_TEST_H
#include "api/oauth_internal.h"
#include "support/test_check.h"
#include <string.h>
#include <stdio.h>
OAuthFlow *oauth_test_open(const wchar_t *fixture);
void oauth_test_close(OAuthFlow *flow);
OAuthResult oauth_test_callback(OAuthFlow *flow, const char *query);
void oauth_test_code(OAuthFlow *flow);
bool oauth_test_zero(const void *data, size_t bytes);
void test_oauth_http(void);
extern uint64_t oauth_test_now;
#endif
