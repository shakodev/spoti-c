#include "ap_session_test.h"
#include "protocol/ap_auth.h"
#include "protocol/proto_codec.h"
#include "keyexchange.pb.h"

static void inspect_login(size_t start, const CredentialRecord *record, bool token)
{
    ApTestFixture *t = &ap_fixture; size_t used;
    CHECK(ap_packet_feed(&t->server, t->fake.output + start, t->fake.written - start, &used) == AP_OK);
    CHECK(used == t->fake.written - start); ApPacketView view;
    CHECK(ap_packet_peek(&t->server, &view) == AP_OK && view.command == AP_CMD_LOGIN);
    ClientResponseEncrypted login = ClientResponseEncrypted_init_zero;
    CHECK(proto_decode(ClientResponseEncrypted_fields, view.body, view.bytes, &login) == PROTO_OK);
    CHECK(login.login_credentials.typ == (AuthenticationType)(token ? 3 : record->ap_type));
    CHECK(login.login_credentials.has_auth_data && strcmp(login.system_info.device_id, record->device_id) == 0);
    CHECK(login.system_info.os == Os_OS_WINDOWS && login.system_info.cpu_family == CpuFamily_CPU_X86_64);
    if (token) { CHECK(!login.login_credentials.has_username && login.login_credentials.auth_data.size == 13 &&
        memcmp(login.login_credentials.auth_data.bytes, "token-fixture", 13) == 0); }
    else { CHECK(login.login_credentials.has_username && strcmp(login.login_credentials.username, record->username) == 0 &&
        login.login_credentials.auth_data.size == record->ap_bytes && memcmp(login.login_credentials.auth_data.bytes, record->ap_blob, record->ap_bytes) == 0); }
    SecureZeroMemory(&login, sizeof(login)); CHECK(ap_packet_consume(&t->server) == AP_OK);
}
static void welcome(const char *name, AuthenticationType type, const char *blob)
{
    APWelcome message = APWelcome_init_zero;
    CHECK(strlen(name) < sizeof(message.canonical_username)); memcpy(message.canonical_username, name, strlen(name) + 1);
    message.account_type_logged_in = AccountType_Spotify; message.credentials_type_logged_in = AccountType_Spotify;
    message.reusable_auth_credentials_type = type; message.reusable_auth_credentials.size = (pb_size_t)strlen(blob);
    memcpy(message.reusable_auth_credentials.bytes, blob, strlen(blob));
    uint8_t wire[4096]; size_t bytes;
    CHECK(proto_encode(APWelcome_fields, &message, wire, sizeof(wire), &bytes) == PROTO_OK);
    ap_fixture_server_packet(AP_CMD_WELCOME, wire, bytes, false); SecureZeroMemory(&message, sizeof(message)); SecureZeroMemory(wire, sizeof(wire));
}
static void reconnect(void)
{
    ApTestFixture *t = &ap_fixture;
    t->now += ap_next_wait_ms(t->session);
    memcpy(t->fake.input, av_response, sizeof(av_response)); t->fake.available = sizeof(av_response);
    CHECK(ap_fixture_event(AP_EVENT_TIMER) == AP_OK && ap_state(t->session) == AP_STATE_CONNECT);
    CHECK(crypto_dh_clear(&t->session->dh) == CRYPTO_OK);
    ap_test_import(&t->session->dh, &t->session->crypto, av_private, av_public);
    memcpy(t->session->nonce, av_nonce, 16); t->fake.connected = true;
    ap_packet_clear(&t->server); CHECK(ap_packet_init(&t->server, av_rx, av_tx) == AP_OK);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && ap_state(t->session) == AP_STATE_FRAMED);
}
static void token_to_stored(void)
{
    CredentialRecord record; CHECK(credential_record_init(&record, "fixture-client") == CREDENTIAL_OK);
    memcpy(record.refresh_token, "oauth-refresh", 14); char device[41]; memcpy(device, record.device_id, 41);
    ApAuth auth; CHECK(ap_auth_init(&auth, &record, "token-fixture") == AP_OK);
    ap_fixture_open(); ap_fixture_framed(); ApTestFixture *t = &ap_fixture; t->session->config.auth = &auth;
    size_t start = t->fake.written; CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING); inspect_login(start, &record, true);
    welcome("canonical-first", AuthenticationType_AUTHENTICATION_STORED_FACEBOOK_CREDENTIALS, "stored-one");
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_RECONNECT && ap_state(t->session) == AP_STATE_BACKOFF);
    CHECK(t->fake.callbacks == 0 && ap_auth_result(&auth) == AP_PENDING && ap_test_zero(auth.token, sizeof(auth.token)));
    CHECK(record.ap_type == 2 && strcmp(record.username, "canonical-first") == 0);
    reconnect(); CHECK(t->fake.begins == 2); inspect_login(sizeof(av_hello) + sizeof(av_reply), &record, false);
    welcome("canonical-changed", AuthenticationType_AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS, "stored-two");
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && ap_state(t->session) == AP_STATE_AUTHENTICATED);
    CHECK(ap_auth_result(&auth) == AP_OK && t->fake.callbacks == 1 && record.ap_type == 1);
    CHECK(strcmp(record.username, "canonical-changed") == 0 && strcmp(record.refresh_token, "oauth-refresh") == 0 && strcmp(record.device_id, device) == 0);
    ap_fixture_close(); ap_auth_clear(&auth);
    /* A subsequent process/session starts directly with the stored type/blob. */
    CHECK(ap_auth_init(&auth, &record, NULL) == AP_OK); ap_fixture_open(); ap_fixture_framed(); t = &ap_fixture;
    t->session->config.auth = &auth; start = t->fake.written;
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING); inspect_login(start, &record, false);
    welcome("canonical-changed", AuthenticationType_AUTHENTICATION_STORED_SPOTIFY_CREDENTIALS, "stored-three");
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING && t->fake.begins == 1 && ap_auth_result(&auth) == AP_OK);
    ap_fixture_close(); ap_auth_clear(&auth); credential_record_clear(&record);
}
static void rejection_and_invalid(void)
{
    CredentialRecord record; CHECK(credential_record_init(&record, "fixture-client") == CREDENTIAL_OK);
    CredentialRecord before = record; ApAuth auth;
    CHECK(ap_auth_init(&auth, &record, NULL) == AP_INVALID);
    CHECK(ap_auth_init(&auth, &record, "token-fixture") == AP_OK);
    ap_fixture_open(); ap_fixture_framed(); ApTestFixture *t = &ap_fixture; t->session->config.auth = &auth;
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    APLoginFailed failure = APLoginFailed_init_zero; failure.error_code = ErrorCode_BadCredentials;
    uint8_t wire[128]; size_t bytes;
    CHECK(proto_encode(APLoginFailed_fields, &failure, wire, sizeof(wire), &bytes) == PROTO_OK);
    ap_fixture_server_packet(AP_CMD_AUTH_FAILURE, wire, bytes, false);
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_AUTH_FAILED && ap_auth_result(&auth) == AP_AUTH_FAILED);
    CHECK(ap_state(t->session) == AP_STATE_CLOSED && ap_test_zero(auth.token, sizeof(auth.token)) && memcmp(&record, &before, sizeof(record)) == 0);
    ap_fixture_close(); ap_auth_clear(&auth);
    CHECK(ap_auth_init(&auth, &record, "token-fixture") == AP_OK);
    ap_fixture_open(); ap_fixture_framed(); t = &ap_fixture; t->session->config.auth = &auth;
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_PENDING);
    welcome("wrong-type", AuthenticationType_AUTHENTICATION_SPOTIFY_TOKEN, "not-reusable");
    CHECK(ap_fixture_event(AP_EVENT_SOCKET) == AP_MALFORMED && memcmp(&record, &before, sizeof(record)) == 0 && ap_test_zero(auth.token, sizeof(auth.token)));
    ap_fixture_close(); ap_auth_clear(&auth); credential_record_clear(&record); credential_record_clear(&before);
}
void test_ap_auth(void) { token_to_stored(); rejection_and_invalid(); }
