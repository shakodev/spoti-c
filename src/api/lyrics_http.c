#include "lyrics_internal.h"
#include <string.h>
#include <wchar.h>

LyricsResult lyrics_http_start(LyricsService *s, LyricsSlot *slot)
{
    const ApiContract *contract = contract_inspect(API_LYRICS);
    const wchar_t *bearer;
    if (s->config.fixture_origin != NULL) { bearer = L"Bearer fixture"; }
    else {
        TokenResult auth = token_authorize(s->config.tokens, slot->url, &slot->authorization);
        if (auth == TOKEN_PENDING) { return LYRICS_PENDING; }
        if (auth != TOKEN_OK) { return LYRICS_AUTH; }
        bearer = slot->authorization.headers[0].value;
    }
    HttpHeader headers[5]; size_t count = 0;
    for (size_t i = 0; i < contract->header_count; ++i) {
        const ApiField *field = &contract->headers[i]; const wchar_t *value = NULL;
        if (strcmp(field->name, "Authorization") == 0) { value = bearer; }
        else if (strcmp(field->name, "Accept") == 0) { value = L"application/json"; }
        else if (strcmp(field->name, "App-Platform") == 0) { value = L"Win32_x86_64"; }
        else if (strcmp(field->name, "Spotify-App-Version") == 0) { value = s->config.app_version; }
        else if (strcmp(field->name, "Accept-Language") == 0) { value = s->config.accept_language; }
        if (value == NULL) { if (field->required) { return LYRICS_INVALID; } continue; }
        if (count == 5 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, field->name, -1, slot->header_names[count], 32) == 0) {
            return LYRICS_TOO_LARGE;
        }
        headers[count] = (HttpHeader){slot->header_names[count], value}; ++count;
    }
    HttpRequest request = {.url = slot->url, .method = HTTP_GET, .headers = headers, .header_count = count,
        .body = slot->page.input, .body_capacity = JSON_PAGE_BODY_CAP, .body_limit = JSON_PAGE_BODY_CAP};
    HttpResult r = http_submit(s->config.http, &request, &slot->http_id);
    if (r != HTTP_OK) {
        token_authorization_clear(&slot->authorization);
        return r == HTTP_FULL ? LYRICS_FULL : LYRICS_ERROR;
    }
    slot->http_active = true; return LYRICS_PENDING;
}

LyricsResult lyrics_poll(LyricsService *s, const LyricsDocument **output)
{
    if (output == NULL) { return LYRICS_INVALID; }
    *output = NULL;
    if (s == NULL || s->config.http == NULL) { return LYRICS_INVALID; }
    (void)http_pump(s->config.http); bool pending = false;
    for (uint32_t i = 0; i < 2; ++i) {
        LyricsSlot *slot = &s->slots[i];
        if (!slot->pending) { continue; }
        pending = true; LyricsTicket ticket = {i, slot->serial};
        if (!slot->http_active) {
            if (slot->cancelled || s->closing || slot->page.generation != s->generation) {
                return lyrics_finish(s, ticket, LYRICS_CANCELLED, 0, 0, output);
            }
            if (s->config.fixture_origin == NULL) { (void)token_poll(s->config.tokens); }
            LyricsResult started = lyrics_http_start(s, slot);
            if (started != LYRICS_PENDING) { return lyrics_finish(s, ticket, started, 0, 0, output); }
        }
        if (!slot->http_active) { continue; }
        if ((slot->cancelled || s->closing || slot->page.generation != s->generation)) { (void)http_cancel(s->config.http, slot->http_id); }
        HttpCompletion c;
        HttpResult r = http_consume(s->config.http, slot->http_id, &c);
        if (r == HTTP_PENDING) { continue; }
        slot->http_active = false;
        if (r != HTTP_OK || !c.final) { return lyrics_finish(s, ticket, LYRICS_ERROR, 0, 0, output); }
        if (slot->authorization.issued) { (void)token_report_status(s->config.tokens, &slot->authorization, c.status); }
        LyricsResult result = c.result == HTTP_CANCELLED ? LYRICS_CANCELLED : c.result == HTTP_BODY_TOO_LARGE ? LYRICS_TOO_LARGE :
            c.result == HTTP_OK || c.result == HTTP_STATUS_ERROR || c.result == HTTP_AUTH ||
            c.result == HTTP_RATE_LIMIT || c.result == HTTP_SERVER_ERROR ? LYRICS_OK : LYRICS_ERROR;
        return lyrics_finish(s, ticket, result, c.status, c.bytes, output);
    }
    return pending ? LYRICS_PENDING : LYRICS_OK;
}
