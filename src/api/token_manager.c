#include "token_internal.h"
#include <string.h>

static void wipe_challenge(TokenManager *m)
{
    SecureZeroMemory(m->context, sizeof(m->context)); SecureZeroMemory(m->prefix, sizeof(m->prefix));
    SecureZeroMemory(m->suffix, sizeof(m->suffix)); SecureZeroMemory(m->challenge_state, sizeof(m->challenge_state));
    m->context_bytes = m->prefix_bytes = 0; m->hash_counter = m->hash_seed = m->hash_started = 0;
}
void token_wipe_job(TokenManager *m)
{
    if (m->http_active) { return; }
    SecureZeroMemory(m->upload, sizeof(m->upload)); SecureZeroMemory(m->response, sizeof(m->response));
    SecureZeroMemory(&m->scratch, sizeof(m->scratch));
}
TokenResult token_fail(TokenManager *m, TokenResult result)
{
    m->result = result; m->stage = m->shutdown ? TS_CLOSING : TS_FAILED;
    m->deadline = CORE_CLOCK_NEVER; m->want_client = m->want_bearer = false;
    SecureZeroMemory(m->slots, sizeof(m->slots)); SecureZeroMemory(m->domains, sizeof(m->domains)); m->domain_count = 0;
    wipe_challenge(m); token_wipe_job(m);
    if (m->http_active) { (void)http_cancel(m->config.http, m->request_id); }
    return result;
}
TokenResult token_clock(TokenManager *m)
{
    uint64_t now = core_clock_now(&m->config.clock);
    if (now < m->now) { m->now = now; return token_fail(m, TOKEN_CLOCK_CHANGED); }
    m->now = now; return TOKEN_OK;
}
static bool fresh(const TokenManager *m, TokenKind kind)
{
    return m->slots[kind].info.present && !core_deadline_expired(m->now, m->slots[kind].info.refresh_at);
}
TokenResult token_manager_create(Arena *arena, const TokenConfig *c, TokenManager **output)
{
    if (output == NULL) { return TOKEN_INVALID; }
    *output = NULL;
    if (arena == NULL || arena->capacity > TOKEN_SECRET_LIMIT || c == NULL || c->http == NULL ||
        !credential_record_valid(c->credentials) || !token_text_valid(c->client_id, CREDENTIAL_CLIENT_CAP, 128) ||
        !token_text_valid(c->client_version, 65, 64) || c->windows_major == 0 || c->windows_major > INT32_MAX ||
        c->windows_build == 0 || c->windows_build > INT32_MAX || c->service_origin_count > TOKEN_ORIGIN_COUNT ||
        c->challenge_iterations > 262144 || c->challenge_timeout_ms > 5000) { return TOKEN_INVALID; }
    if (strcmp(c->client_id, c->credentials->client_id) != 0) { return TOKEN_CLIENT_MISMATCH; }
    char host[256];
    for (size_t i = 0; i < c->service_origin_count; ++i) {
        if (!token_url_host(c->service_origins[i], true, false, host)) { return TOKEN_INVALID; }
        size_t n = strlen(host);
        if (n < 11 || strcmp(host + n - 11, "spotify.com") != 0 || (n > 11 && host[n - 12] != '.')) { return TOKEN_SCOPE; }
    }
    bool fixture = c->fixture_clienttoken_url != NULL || c->fixture_login5_url != NULL;
    if (fixture && (!token_url_host(c->fixture_clienttoken_url, false, true, host) ||
        !token_url_host(c->fixture_login5_url, false, true, host))) { return TOKEN_INVALID; }
    TokenManager *m = arena_alloc(arena, sizeof(*m), _Alignof(TokenManager));
    if (m == NULL) { return TOKEN_FULL; }
    *m = (TokenManager){0}; m->config = *c;
    memcpy(m->client_id, c->client_id, strlen(c->client_id) + 1); memcpy(m->version, c->client_version, strlen(c->client_version) + 1);
    if (m->config.challenge_iterations == 0) { m->config.challenge_iterations = 65536; }
    if (m->config.challenge_timeout_ms == 0) { m->config.challenge_timeout_ms = 2000; }
    m->now = core_clock_now(&c->clock); m->deadline = CORE_CLOCK_NEVER; *output = m; return TOKEN_OK;
}
TokenResult token_install(TokenManager *m, TokenKind kind, const char *text, uint32_t expiry, uint32_t refresh)
{
    size_t cap = kind == TOKEN_CLIENT ? TOKEN_TEXT_CAP - 1 : TOKEN_TEXT_CAP - 8;
    if (!token_text_valid(text, 4096, cap)) { return TOKEN_FULL; }
    if (expiry == 0 || expiry > 86400 || refresh == 0 || refresh > expiry) { return TOKEN_MALFORMED; }
    if (m->sequence == UINT64_MAX) { return TOKEN_EXHAUSTED; }
    TokenSlot *slot = &m->slots[kind]; SecureZeroMemory(slot, sizeof(*slot));
    memcpy(slot->text, text, strlen(text) + 1);
    slot->info = (TokenInfo){core_deadline_after(m->now, (uint64_t)expiry * 1000),
        core_deadline_after(m->now, (uint64_t)refresh * 1000), ++m->sequence, true};
    m->stage = TS_IDLE; m->deadline = CORE_CLOCK_NEVER; wipe_challenge(m); return TOKEN_OK;
}
TokenResult token_submit(TokenManager *m, bool login, size_t bytes)
{
    if (m->http_active || bytes == 0 || bytes > sizeof(m->upload)) { return TOKEN_INVALID; }
    HttpHeader headers[4] = {{L"Content-Type", L"application/x-protobuf"}, {L"Accept", L"application/x-protobuf"},
        {L"Cache-Control", L"no-cache, no-store, max-age=0"}, {L"client-token", m->scratch.header}};
    if (login && (!token_domain_granted(m, "login5.spotify.com") ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m->slots[TOKEN_CLIENT].text, -1, m->scratch.header, TOKEN_TEXT_CAP) == 0)) {
        return TOKEN_SCOPE;
    }
    const wchar_t *url = login ? L"https://login5.spotify.com/v3/login" : L"https://clienttoken.spotify.com/v1/clienttoken";
    if (m->config.fixture_clienttoken_url != NULL) { url = login ? m->config.fixture_login5_url : m->config.fixture_clienttoken_url; }
    HttpRequest request = {.url = url, .method = HTTP_POST, .headers = headers, .header_count = login ? 4 : 3,
        .upload = m->upload, .upload_bytes = bytes, .body = m->response, .body_capacity = sizeof(m->response),
        .body_limit = sizeof(m->response), .timeout_ms = core_deadline_wait(m->now, m->deadline)};
    HttpResult result = http_submit(m->config.http, &request, &m->request_id);
    SecureZeroMemory(&m->scratch, sizeof(m->scratch));
    if (result != HTTP_OK) { return result == HTTP_FULL ? TOKEN_FULL : TOKEN_HTTP_ERROR; }
    m->http_active = true; m->stage = login ? TS_LOGIN_HTTP : TS_CLIENT_HTTP; m->result = TOKEN_PENDING; return TOKEN_OK;
}
TokenResult token_schedule(TokenManager *m)
{
    if (m->shutdown) { return TOKEN_CLOSED; }
    if (m->stage == TS_FAILED) { return m->result; }
    if (m->http_active || m->stage == TS_CLIENT_HASH || m->stage == TS_LOGIN_HASH) { return TOKEN_PENDING; }
    TokenResult result = TOKEN_OK;
    if ((m->want_client || m->want_bearer) && !fresh(m, TOKEN_CLIENT)) { result = token_client_start(m, false); }
    else if (m->want_bearer && !fresh(m, TOKEN_BEARER)) { result = token_login_start(m, false); }
    else { m->want_client = m->want_bearer = false; m->result = TOKEN_OK; return TOKEN_OK; }
    return result == TOKEN_OK ? TOKEN_PENDING : token_fail(m, result);
}
TokenResult token_ensure(TokenManager *m, TokenKind kind)
{
    if (m == NULL || (kind != TOKEN_CLIENT && kind != TOKEN_BEARER)) { return TOKEN_INVALID; }
    if (m->shutdown) { return TOKEN_CLOSED; }
    TokenResult result = token_clock(m); if (result != TOKEN_OK) { return result; }
    if (!credential_record_valid(m->config.credentials) || strcmp(m->client_id, m->config.credentials->client_id) != 0) {
        return token_fail(m, TOKEN_CLIENT_MISMATCH);
    }
    if (m->stage == TS_FAILED) { return m->result; }
    if (kind == TOKEN_BEARER && m->config.credentials->ap_type != 1) { return token_fail(m, TOKEN_UNSUPPORTED); }
    if (fresh(m, kind) && fresh(m, TOKEN_CLIENT)) { return TOKEN_OK; }
    if (kind == TOKEN_BEARER) { m->want_bearer = true; } else { m->want_client = true; }
    return token_schedule(m);
}
TokenResult token_poll(TokenManager *m)
{
    if (m == NULL) { return TOKEN_INVALID; }
    if (m->stage == TS_CLOSED) { return TOKEN_CLOSED; }
    if (!m->shutdown) {
        (void)token_clock(m);
        if (m->stage != TS_FAILED && core_deadline_expired(m->now, m->deadline)) { (void)token_fail(m, TOKEN_TIMEOUT); }
    }
    if (m->http_active) {
        if (http_pump(m->config.http) != HTTP_OK) { (void)token_fail(m, TOKEN_HTTP_ERROR); }
        HttpCompletion completion;
        HttpResult result = http_consume(m->config.http, m->request_id, &completion);
        if (result == HTTP_PENDING) { return TOKEN_PENDING; }
        if (result != HTTP_OK) { return token_fail(m, TOKEN_HTTP_ERROR); }
        TokenStage stage = m->stage; m->http_active = false;
        if (stage == TS_CLOSING || stage == TS_FAILED) { token_wipe_job(m); return m->result; }
        if (!completion.final || completion.result != HTTP_OK || completion.status != 200) {
            return token_fail(m, completion.status == 401 || completion.status == 403 ? TOKEN_REJECTED : TOKEN_HTTP_ERROR);
        }
        TokenResult parsed = stage == TS_CLIENT_HTTP ? token_client_response(m, completion.bytes) : token_login_response(m, completion.bytes);
        token_wipe_job(m);
        if (parsed != TOKEN_OK && parsed != TOKEN_PENDING) { return token_fail(m, parsed); }
    }
    if (m->stage == TS_CLIENT_HASH || m->stage == TS_LOGIN_HASH) {
        TokenResult result = token_hash_step(m);
        if (result != TOKEN_OK && result != TOKEN_PENDING) { return token_fail(m, result); }
    }
    return token_schedule(m);
}
HANDLE token_wait_handle(const TokenManager *m) { return m == NULL || !m->http_active ? NULL : http_wait_handle(m->config.http); }
uint32_t token_next_wait_ms(const TokenManager *m)
{
    if (m == NULL || m->stage == TS_CLOSED) { return INFINITE; }
    if (m->stage == TS_CLIENT_HASH || m->stage == TS_LOGIN_HASH) { return 0; }
    if (!m->http_active) { return INFINITE; }
    uint32_t wait = core_deadline_wait(core_clock_now(&m->config.clock), m->deadline), http_ms = http_next_wait_ms(m->config.http);
    return wait < http_ms ? wait : http_ms;
}
TokenInfo token_info(const TokenManager *m, TokenKind kind)
{
    if (m == NULL || (kind != TOKEN_CLIENT && kind != TOKEN_BEARER)) { return (TokenInfo){0}; }
    TokenInfo info = m->slots[kind].info;
    uint64_t now = core_clock_now(&m->config.clock);
    if (now < m->now || core_deadline_expired(now, info.expires_at)) { info.present = false; }
    return info;
}
TokenResult token_result(const TokenManager *m) { return m == NULL ? TOKEN_INVALID : m->result; }
TokenResult token_retry_refresh(TokenManager *m)
{
    if (m == NULL) { return TOKEN_INVALID; }
    if (m->shutdown) { return TOKEN_CLOSED; }
    if (m->http_active) { return TOKEN_PENDING; }
    if (m->stage != TS_FAILED) { return TOKEN_INVALID; }
    m->stage = TS_IDLE; m->result = TOKEN_OK; return TOKEN_OK;
}
TokenResult token_manager_shutdown(TokenManager *m)
{
    if (m == NULL) { return TOKEN_INVALID; }
    if (m->stage == TS_CLOSED) { return TOKEN_OK; }
    if (!m->shutdown) { m->shutdown = true; (void)token_fail(m, TOKEN_CANCELLED); }
    if (m->http_active) { (void)token_poll(m); if (m->http_active) { return TOKEN_PENDING; } }
    token_wipe_job(m); SecureZeroMemory(m, sizeof(*m)); m->stage = TS_CLOSED; m->shutdown = true; m->result = TOKEN_CLOSED; return TOKEN_OK;
}
TokenResult token_logout(TokenManager *m)
{
    if (m == NULL) { return TOKEN_INVALID; }
    if (!m->shutdown) { credential_record_clear(m->config.credentials); }
    m->logout = true; return token_manager_shutdown(m);
}
bool token_balanced(const TokenManager *m) { return m != NULL && m->stage == TS_CLOSED && !m->http_active; }
