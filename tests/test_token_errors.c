#include "token_test.h"

static void bad_messages(void)
{
    const uint8_t malformed[] = {0x0a, 0x80};
    TestHttpResponse responses[] = {TOKEN_RESPONSE_FIXTURE(client_bad_refresh), TOKEN_RESPONSE_FIXTURE(client_long_token),
        TOKEN_RESPONSE_FIXTURE(client_no_domains),
        {.body = malformed, .bytes = sizeof(malformed)}};
    TokenResult expected[] = {TOKEN_MALFORMED, TOKEN_FULL, TOKEN_SCOPE, TOKEN_MALFORMED};
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        token_test_open(&responses[i], 1);
        CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == expected[i]);
        CHECK(token_test_zero(token_test.manager->slots, sizeof(token_test.manager->slots)) && !token_test.manager->http_active);
        token_test_close();
    }
    TestHttpResponse login[] = {TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(login_rejected)};
    token_test_open(login, 2);
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_REJECTED);
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_REJECTED && atomic_load(&token_test.server.accepted) == 2);
    token_test_close();
    login[1] = TOKEN_RESPONSE_FIXTURE(login_timeout); token_test_open(login, 2);
    CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING && token_test_finish() == TOKEN_TIMEOUT); token_test_close();
    TestHttpResponse rejected[] = {TOKEN_RESPONSE_FIXTURE(login_wrong_user), TOKEN_RESPONSE_FIXTURE(login_long_token)};
    for (size_t i = 0; i < 2; ++i) {
        login[1] = rejected[i]; token_test_open(login, 2);
        CHECK(token_ensure(token_test.manager, TOKEN_BEARER) == TOKEN_PENDING &&
            token_test_finish() == (i == 0 ? TOKEN_REJECTED : TOKEN_FULL));
        CHECK(token_test_zero(token_test.manager->slots, sizeof(token_test.manager->slots))); token_test_close();
    }
}
static void http_failures(void)
{
    const char *statuses[] = {"HTTP/1.1 401 Unauthorized\r\n", "HTTP/1.1 403 Forbidden\r\n", "HTTP/1.1 500 Internal Server Error\r\n",
        "HTTP/1.1 302 Found\r\nLocation: https://evil.invalid/\r\n"};
    for (size_t i = 0; i < 4; ++i) {
        TestHttpResponse responses[] = {TOKEN_RESPONSE_FIXTURE(client_granted), TOKEN_RESPONSE_FIXTURE(client_granted)};
        responses[0].headers = statuses[i]; token_test_open(responses, 2);
        TokenResult expected = i < 2 ? TOKEN_REJECTED : TOKEN_HTTP_ERROR;
        CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == expected);
        CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == expected && atomic_load(&token_test.server.accepted) == 1);
        CHECK(token_retry_refresh(token_test.manager) == TOKEN_OK);
        CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
        CHECK(atomic_load(&token_test.server.accepted) == 2); token_test_close();
    }
}
static void creation_and_domains(void)
{
    TestHttpResponse response = TOKEN_RESPONSE_FIXTURE(client_granted); token_test_open(&response, 1);
    TokenFixture *t = &token_test; TokenManager *m; TokenConfig config = t->config; size_t used = t->secret.used;
    config.client_id = "different-client"; CHECK(token_manager_create(&t->secret, &config, &m) == TOKEN_CLIENT_MISMATCH && m == NULL);
    config = t->config; config.service_origins[0] = L"https://audio.scdn.co";
    CHECK(token_manager_create(&t->secret, &config, &m) == TOKEN_SCOPE && m == NULL);
    config = t->config; config.service_origins[0] = L"https://api-partner.spotify.com/path";
    CHECK(token_manager_create(&t->secret, &config, &m) == TOKEN_INVALID && m == NULL);
    config = t->config; config.fixture_login5_url = L"http://127.0.0.1:80@evil/login";
    CHECK(token_manager_create(&t->secret, &config, &m) == TOKEN_INVALID && m == NULL);
    config = t->config; config.challenge_iterations = 262145;
    CHECK(token_manager_create(&t->secret, &config, &m) == TOKEN_INVALID && m == NULL && t->secret.used == used);
    uint8_t tiny[8]; Arena arena; CHECK(arena_init(&arena, tiny, sizeof(tiny)));
    CHECK(token_manager_create(&arena, &t->config, &m) == TOKEN_FULL && m == NULL); arena_dispose(&arena);
    CHECK(token_ensure(t->manager, TOKEN_CLIENT) == TOKEN_PENDING && token_test_finish() == TOKEN_OK);
    CHECK(token_domain_granted(t->manager, "api-partner.spotify.com") && !token_domain_granted(t->manager, "evilspotify.com") &&
        !token_domain_granted(t->manager, "spotify.com.evil"));
    memcpy(t->manager->domains[0], "api-partner.spotify.com", sizeof("api-partner.spotify.com"));
    CHECK(token_ensure(t->manager, TOKEN_BEARER) == TOKEN_SCOPE && !t->manager->http_active && atomic_load(&t->server.accepted) == 1);
    token_test_close();
}
static void cancellation(void)
{
    for (unsigned logout = 0; logout < 2; ++logout) {
        TestHttpResponse response = TOKEN_RESPONSE_FIXTURE(client_granted); response.hold_body = true;
        token_test_open(&response, 1); TokenFixture *t = &token_test;
        CHECK(token_ensure(t->manager, TOKEN_CLIENT) == TOKEN_PENDING && WaitForSingleObject(t->server.headers_sent, 3000) == WAIT_OBJECT_0);
        TokenResult result = logout ? token_logout(t->manager) : token_manager_shutdown(t->manager);
        CHECK(result == TOKEN_PENDING || result == TOKEN_OK);
        CHECK(token_ensure(t->manager, TOKEN_CLIENT) == TOKEN_CLOSED);
        TokenAuthorization authorization = {0};
        CHECK(token_authorize(t->manager, L"https://api-partner.spotify.com/", &authorization) == TOKEN_CLOSED && !authorization.issued);
        CHECK((t->record.ap_bytes == 0) == (logout != 0));
        if (t->manager->http_active) { CHECK(!token_test_zero(t->manager->upload, sizeof(t->manager->upload))); }
        CHECK(SetEvent(t->server.release_body)); token_test_close();
    }
    TestHttpResponse response = TOKEN_RESPONSE_FIXTURE(client_granted); response.hold_body = true;
    token_test_open(&response, 1);
    CHECK(token_ensure(token_test.manager, TOKEN_CLIENT) == TOKEN_PENDING && WaitForSingleObject(token_test.server.headers_sent, 3000) == WAIT_OBJECT_0);
    token_test.now += 30000; (void)token_poll(token_test.manager);
    CHECK(token_result(token_test.manager) == TOKEN_TIMEOUT && SetEvent(token_test.server.release_body));
    CHECK(token_test_finish() == TOKEN_TIMEOUT); token_test_close();
}
void test_token_errors(void) { bad_messages(); http_failures(); creation_and_domains(); cancellation(); }
