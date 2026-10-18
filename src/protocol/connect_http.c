#include "connect_internal.h"
#include <wchar.h>

static bool origin_valid(const wchar_t *origin)
{
    if (!origin || wcsncmp(origin, L"https://", 8)) { return false; }
    const wchar_t *host = origin + 8; size_t n = wcsnlen_s(host, 254);
    if (n <= 12 || n == 254 || wcscmp(host + n - 12, L".spotify.com")) { return false; }
    for (size_t i = 0; i < n; ++i) {
        wchar_t c = host[i]; if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'.')) { return false; }
    }
    return true;
}
static bool fixture_origin(const wchar_t *origin)
{
    if (!origin || wcsncmp(origin, L"http://127.0.0.1:", 17)) { return false; }
    const wchar_t *p = origin + 17; unsigned port = 0;
    if (!*p) { return false; }
    for (; *p; ++p) {
        if (*p < L'0' || *p > L'9' || port > 6553) { return false; }
        port = port * 10 + (unsigned)(*p - L'0'); if (port > 65535) { return false; }
    }
    return port != 0;
}
DealerResult connect_poll(ConnectState *s)
{
    if (!s) { return DEALER_INVALID; }
    if (s->closing || s->logged_out) {
        if (connect_shutdown(s) == DEALER_PENDING) { return DEALER_PENDING; }
        if (s->logged_out && s->config.tokens && token_logout(s->config.tokens) == TOKEN_PENDING) { return DEALER_PENDING; }
        return DEALER_CLOSED;
    }
    PlayerQueueSnapshot queue; player_take_state(s->config.player, &queue);
    PlayerSnapshot current = queue.player, previous = s->observed.player;
    if (current.generation != previous.generation || current.state != previous.state || current.rate != previous.rate ||
        current.position_frames != previous.position_frames || current.duration_frames != previous.duration_frames || current.volume != previous.volume ||
        queue.count != s->observed.count || queue.index != s->observed.index ||
        memcmp(queue.ids, s->observed.ids, queue.count * 16)) { connect_changed(s); }
    s->observed = queue;
    if (s->config.fixture_replay) { connect_publish(s); return DEALER_OK; }
    (void)http_pump(s->config.http);
    if (s->pending) {
        HttpCompletion completion; HttpResult r = http_consume(s->config.http, s->request, &completion);
        if (r == HTTP_PENDING) { return DEALER_PENDING; }
        s->pending = false;
        if (r != HTTP_OK) { s->registered = false; connect_publish(s); token_authorization_clear(&s->auth); return DEALER_IO; }
        TokenResult auth = s->config.fixture_http_origin ? (completion.status == 401 ? TOKEN_RETRY : TOKEN_OK) :
            token_report_status(s->config.tokens, &s->auth, completion.status);
        if (auth == TOKEN_RETRY && !s->retry) { s->retry = true; s->dirty = true; return DEALER_PENDING; }
        token_authorization_clear(&s->auth);
        if (completion.status == 401 || completion.status == 403) { s->registered = false; connect_publish(s); return DEALER_AUTH; }
        if (completion.result != HTTP_OK || completion.status < 200 || completion.status >= 300) { return DEALER_IO; }
        s->registered = s->sent_epoch == s->connection_epoch && s->connection_id[0]; s->retry = false;
        if (s->registered && completion.bytes) {
            spotify_connectstate_Cluster cluster = spotify_connectstate_Cluster_init_zero;
            if (proto_decode(spotify_connectstate_Cluster_fields, s->response, completion.bytes, &cluster) != PROTO_OK) {
                s->registered = false; connect_publish(s); return DEALER_MALFORMED;
            }
            DealerResult applied = connect_cluster(s, &cluster);
            if (applied != DEALER_OK && applied != DEALER_STALE) { return applied; }
        }
        if (s->revision != s->sent_revision) { s->dirty = true; }
        connect_publish(s);
    }
    if (!s->dirty || !s->connection_id[0]) { return DEALER_OK; }
    const wchar_t *origin = s->config.fixture_http_origin ? s->config.fixture_http_origin : s->config.spclient_origin;
    if (s->config.fixture_http_origin ? !fixture_origin(origin) : !origin_valid(origin)) { return DEALER_INVALID; }
    if (swprintf_s(s->url, HTTP_URL_CHARS, L"%s/connect-state/v1/devices/%S", origin, s->config.device_id) < 0) { return DEALER_LIMIT; }
    if (s->config.fixture_http_origin) {
        s->auth.headers[0] = (HttpHeader){L"Authorization", L"Bearer fixture"};
        s->auth.headers[1] = (HttpHeader){L"client-token", L"fixture-client-token"};
    } else {
        (void)token_poll(s->config.tokens);
        TokenResult auth = token_authorize(s->config.tokens, s->url, &s->auth);
        if (auth != TOKEN_OK) { return auth == TOKEN_PENDING || auth == TOKEN_FULL ? DEALER_PENDING : DEALER_AUTH; }
    }
    size_t bytes; DealerResult result = connect_encode(s, s->upload, sizeof(s->upload), &bytes);
    if (result != DEALER_OK) { token_authorization_clear(&s->auth); return result; }
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s->connection_id, -1, s->connection_header, 257)) { return DEALER_INVALID; }
    HttpHeader headers[] = {s->auth.headers[0], s->auth.headers[1],
        {L"X-Spotify-Connection-Id", s->connection_header}, {L"Content-Type", L"application/x-protobuf"}};
    HttpRequest request = {.url = s->url, .method = HTTP_PUT, .headers = headers, .header_count = 4,
        .upload = s->upload, .upload_bytes = bytes, .body = s->response, .body_capacity = sizeof(s->response), .timeout_ms = 15000};
    HttpResult r = http_submit(s->config.http, &request, &s->request);
    if (r == HTTP_FULL) { token_authorization_clear(&s->auth); return DEALER_FULL; }
    if (r != HTTP_OK && r != HTTP_PENDING) { token_authorization_clear(&s->auth); return DEALER_IO; }
    s->pending = true; s->dirty = false; s->sent_revision = s->revision; s->sent_epoch = s->connection_epoch;
    ++s->sequence; return DEALER_PENDING;
}
