#include "token_test.h"

static void solved(void)
{
    TestHttpResponse responses[] = {TOKEN_RESPONSE_FIXTURE(client_challenge), TOKEN_RESPONSE_FIXTURE(client_granted),
        TOKEN_RESPONSE_FIXTURE(login_challenge), TOKEN_RESPONSE_FIXTURE(login_ok)};
    token_test_open(responses, 4); CredentialRecord before = token_test.record;
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
    token_test_request(0, auth_client_request, sizeof(auth_client_request), false);
    token_test_request(1, auth_client_answer, sizeof(auth_client_answer), false);
    token_test_request(2, auth_login_request, sizeof(auth_login_request), true);
    token_test_request(3, auth_login_answer, sizeof(auth_login_answer), true);
    CHECK(memcmp(&before, &token_test.record, sizeof(before)) == 0); /* Login5 credential is never substituted for AP data. */
    CHECK(token_test_zero(token_test.manager->context, sizeof(token_test.manager->context)) &&
        token_test_zero(token_test.manager->challenge_state, sizeof(token_test.manager->challenge_state)) &&
        token_test_zero(&token_test.manager->scratch, sizeof(token_test.manager->scratch)));
    credential_record_clear(&before); token_test_close();
}
static void unsupported_and_bounded(void)
{
    TestHttpResponse unsupported = TOKEN_RESPONSE_FIXTURE(client_unsupported);
    token_test_open(&unsupported, 1);
    CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_UNSUPPORTED);
    CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_UNSUPPORTED && atomic_load(&token_test.server.accepted) == 1);
    CHECK(token_next_wait_ms(token_test.manager) == INFINITE); token_test_close();
    TestHttpResponse code[] = {TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(login_unsupported)};
    token_test_open(code, 2);
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_UNSUPPORTED);
    CHECK(atomic_load(&token_test.server.accepted) == 2); token_test_close();
    code[1] = TOKEN_RESPONSE_FIXTURE(login_unknown_variant); token_test_open(code, 2);
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_UNSUPPORTED); token_test_close();
    code[1] = TOKEN_RESPONSE_FIXTURE(login_large_prefix); token_test_open(code, 2);
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_CHALLENGE_LIMIT); token_test_close();
    TestHttpResponse hard = TOKEN_RESPONSE_FIXTURE(client_hard_challenge); token_test_open(&hard, 1);
    CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_CHALLENGE_LIMIT); token_test_close();
    TestHttpResponse work = TOKEN_RESPONSE_FIXTURE(client_work_limit); token_test_open(&work, 1);
    token_test.manager->config.challenge_iterations = 1;
    CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_CHALLENGE_LIMIT); token_test_close();
    TestHttpResponse timed = TOKEN_RESPONSE_FIXTURE(client_challenge); token_test_open(&timed, 1);
    token_test.clock_step = 2; token_test.manager->config.challenge_timeout_ms = 1;
    CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_TIMEOUT); token_test_close();
    TestHttpResponse repeated[] = {TOKEN_RESPONSE_FIXTURE(client_challenge), TOKEN_RESPONSE_FIXTURE(client_challenge), TOKEN_RESPONSE_FIXTURE(client_challenge)};
    token_test_open(repeated, 3);
    CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_CHALLENGE_LIMIT);
    CHECK(atomic_load(&token_test.server.accepted) == 3); token_test_close();
    TestHttpResponse context[] = {TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(login_large_context)};
    token_test_open(context, 2);
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_FULL);
    CHECK(token_test_zero(token_test.manager->context, sizeof(token_test.manager->context))); token_test_close();
}
void test_token_challenges(void) { solved(); unsupported_and_bounded(); }
