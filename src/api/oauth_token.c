#include "oauth_internal.h"
#include <string.h>

static OAuthResult submit(OAuthFlow *f, HttpClient *http, OAuthWriter *w)
{
    if (w->full) { return oauth_terminal(f, OAUTH_FULL); }
    f->upload[--w->used] = 0;
    HttpHeader header = {L"Content-Type", L"application/x-www-form-urlencoded"};
    HttpRequest request = {.url = f->token_url, .method = HTTP_POST, .headers = &header, .header_count = 1,
        .upload = f->upload, .upload_bytes = w->used, .body = f->response, .body_capacity = sizeof(f->response),
        .body_limit = JSON_BASE_INPUT_LIMIT, .timeout_ms = 30000};
    HttpResult result = http_submit(http, &request, &f->request_id);
    if (result != HTTP_OK) { return oauth_terminal(f, result == HTTP_FULL ? OAUTH_FULL : OAUTH_HTTP_ERROR); }
    f->http = http; f->http_active = true; f->state = OAUTH_HTTP; f->result = OAUTH_PENDING;
    /* HTTP owns upload/response through final consume; verifier/code copies
     * are no longer needed outside that borrowed, bounded upload. */
    oauth_wipe_transient(f); return OAUTH_OK;
}
OAuthResult oauth_exchange(OAuthFlow *f, HttpClient *http)
{
    if (f == NULL || f->destroyed || http == NULL || f->state != OAUTH_CODE) { return OAUTH_INVALID; }
    if (core_deadline_expired(core_clock_now(&f->clock), f->deadline)) { return oauth_terminal(f, OAUTH_EXPIRED); }
    OAuthWriter w = {f->upload, sizeof(f->upload), 0, false};
    oauth_pair(&w, "grant_type", "authorization_code"); oauth_pair(&w, "client_id", f->client_id);
    oauth_pair(&w, "code", f->code); oauth_pair(&w, "redirect_uri", f->redirect); oauth_pair(&w, "code_verifier", f->verifier);
    return submit(f, http, &w);
}
OAuthResult oauth_refresh(OAuthFlow *f, HttpClient *http, const CredentialRecord *r)
{
    if (f == NULL || f->destroyed || http == NULL || f->state != OAUTH_IDLE || !credential_record_valid(r) ||
        r->refresh_token[0] == 0 || strcmp(f->client_id, r->client_id) != 0) { return OAUTH_INVALID; }
    memcpy(f->refresh, r->refresh_token, sizeof(f->refresh)); f->refreshing = true;
    f->deadline = core_deadline_after(core_clock_now(&f->clock), f->timeout_ms);
    OAuthWriter w = {f->upload, sizeof(f->upload), 0, false};
    oauth_pair(&w, "grant_type", "refresh_token"); oauth_pair(&w, "client_id", f->client_id);
    oauth_pair(&w, "refresh_token", f->refresh); return submit(f, http, &w);
}
static bool read_string(yyjson_val *root, const char *key, char *out, size_t cap, bool optional)
{
    yyjson_val *value; JsonString text;
    JsonResult result = json_base_field(root, key, &value);
    if (result == JSON_MISSING) { return optional; }
    if (result != JSON_OK || json_base_string(value, cap - 1, &text) != JSON_OK || text.bytes == 0) { return false; }
    for (size_t i = 0; i < text.bytes; ++i) { if ((unsigned char)text.data[i] <= 32 || (unsigned char)text.data[i] >= 127) { return false; } }
    return json_base_string_copy(text, out, cap) == JSON_OK;
}
static bool unique_fields(yyjson_val *root)
{
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) > 16) { return false; }
    yyjson_val *keys[16] = {0}; size_t count = 0, index, maximum; yyjson_val *key, *value;
    yyjson_obj_foreach(root, index, maximum, key, value) {
        (void)value;
        size_t n = yyjson_get_len(key);
        if (memchr(yyjson_get_str(key), 0, n) != NULL) { return false; }
        for (size_t i = 0; i < count; ++i) {
            if (yyjson_get_len(keys[i]) == n && memcmp(yyjson_get_str(keys[i]), yyjson_get_str(key), n) == 0) { return false; }
        }
        keys[count++] = key;
    }
    return true;
}
static OAuthResult parse_token(OAuthFlow *f, size_t bytes)
{
    JsonBase json = {0};
    if (json_base_parse(&json, f->response, bytes, sizeof(f->response), f->json_pool, sizeof(f->json_pool)) != JSON_OK) {
        return OAUTH_BAD_RESPONSE;
    }
    yyjson_val *root = json_base_root(&json), *expires;
    char type[16] = {0}; uint64_t seconds = 0;
    bool valid = unique_fields(root) && read_string(root, "access_token", f->token.access, sizeof(f->token.access), false) &&
        read_string(root, "token_type", type, sizeof(type), false) && strcmp(type, "Bearer") == 0 &&
        read_string(root, "refresh_token", f->refresh, sizeof(f->refresh), f->refreshing) &&
        json_base_field(root, "expires_in", &expires) == JSON_OK && json_base_u64(expires, 86400, &seconds) == JSON_OK && seconds != 0;
    yyjson_val *scope;
    if (json_base_field(root, "scope", &scope) != JSON_MISSING) {
        JsonString text;
        valid = valid && json_base_string(scope, sizeof(f->token.scope) - 1, &text) == JSON_OK &&
            json_base_string_copy(text, f->token.scope, sizeof(f->token.scope)) == JSON_OK;
        for (size_t i = 0; valid && f->token.scope[i] != 0; ++i) { valid = oauth_unreserved(f->token.scope[i]) || f->token.scope[i] == ' '; }
    }
    json_base_close(&json);
    if (!valid) { return OAUTH_BAD_RESPONSE; }
    f->token.expires_at = core_deadline_after(core_clock_now(&f->clock), seconds * 1000); return OAUTH_OK;
}
OAuthResult oauth_token_poll(OAuthFlow *f)
{
    if (http_pump(f->http) != HTTP_OK) { (void)oauth_terminal(f, OAUTH_HTTP_ERROR); return OAUTH_PENDING; }
    HttpCompletion completion;
    HttpResult consumed = http_consume(f->http, f->request_id, &completion);
    if (consumed == HTTP_PENDING) { return OAUTH_PENDING; }
    if (consumed != HTTP_OK) { return oauth_terminal(f, OAUTH_HTTP_ERROR); }
    f->http_active = false; f->http = NULL;
    if (f->state == OAUTH_TERMINAL) { oauth_wipe_transient(f); return f->result; }
    if (!completion.final || completion.result != HTTP_OK || completion.status != 200) {
        return oauth_terminal(f, OAUTH_HTTP_ERROR);
    }
    OAuthResult result = parse_token(f, completion.bytes);
    oauth_wipe_transient(f);
    if (result != OAUTH_OK) { return oauth_terminal(f, result); }
    f->deadline = CORE_CLOCK_NEVER; f->state = OAUTH_DONE; f->result = OAUTH_OK; return OAUTH_OK;
}
OAuthResult oauth_take_token(OAuthFlow *f, CredentialRecord *r, OAuthToken *output)
{
    if (f == NULL || f->destroyed || output == NULL || !credential_record_valid(r) ||
        strcmp(f->client_id, r->client_id) != 0) { return OAUTH_INVALID; }
    if (f->state != OAUTH_DONE || f->taken) { return OAUTH_REPLAY; }
    if (core_deadline_expired(core_clock_now(&f->clock), f->token.expires_at)) { return oauth_terminal(f, OAUTH_EXPIRED); }
    memcpy(r->refresh_token, f->refresh, sizeof(r->refresh_token)); *output = f->token; f->taken = true;
    SecureZeroMemory(&f->token, sizeof(f->token)); SecureZeroMemory(f->refresh, sizeof(f->refresh)); return OAUTH_OK;
}
