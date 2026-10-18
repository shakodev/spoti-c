#include "media_internal.h"
#include <wchar.h>

static MediaResult relink(MediaSlot *s, const MetadataAlternatives *alternatives)
{
    unsigned depth = s->candidates[s->candidate].depth + 1;
    for (size_t i = 0; i < alternatives->count; ++i) {
        bool seen = false;
        for (size_t j = 0; j < s->candidate_count; ++j) { seen |= memcmp(s->candidates[j].gid, alternatives->gids[i], 16) == 0; }
        if (seen) { continue; }
        if (depth >= METADATA_MAX_TRACK_DEPTH || s->candidate_count == METADATA_MAX_TRACKS) { return MEDIA_LIMIT; }
        MediaCandidate *candidate = &s->candidates[s->candidate_count++];
        memcpy(candidate->gid, alternatives->gids[i], 16); candidate->depth = depth;
    }
    if (s->candidate + 1 == s->candidate_count) { return MEDIA_UNSUPPORTED; }
    ++s->candidate; (void)media_gid_to_uri(s->candidates[s->candidate].gid, s->request_uri);
    s->stage = MS_METADATA; return MEDIA_PENDING;
}

static MediaResult body_received(MediaResolver *m, MediaSlot *s, size_t bytes)
{
    uint64_t now = core_clock_now(&m->config.clock);
    if (s->stage == MS_METADATA) {
        MediaPolicy policy = m->config.policy; policy.utc_ms = media_utc(m);
        MetadataAlternatives alternatives = {0};
        MediaResult result = metadata_decode_relink(m->response, bytes, s->request_uri, &policy, &s->source.track, &alternatives);
        if (result == MEDIA_RESTRICTED || result == MEDIA_UNSUPPORTED) {
            MediaResult follow = relink(s, &alternatives);
            if (follow != MEDIA_UNSUPPORTED) { return follow; }
        }
        if (result != MEDIA_OK) { return result; }
        s->stage = MS_KEY; return MEDIA_PENDING;
    }
    if (s->stage == MS_STORAGE) {
        MediaResult result = storage_resolve_decode(m->response, bytes, s->source.track.file_id,
            media_utc(m), now, s->source.urls, &s->source.url_count);
        if (result == MEDIA_OK) { media_finish(s, MEDIA_OK); }
        return result;
    }
    return MEDIA_BAD_STATE;
}

MediaResult media_http_poll(MediaResolver *m)
{
    if (!m->config.http) { return m->active_slot < 0 ? MEDIA_OK : MEDIA_BAD_STATE; }
    if (http_pump(m->config.http) != HTTP_OK) { return MEDIA_HTTP_ERROR; }
    if (m->active_slot < 0) { return MEDIA_OK; }
    HttpCompletion completion;
    HttpResult consumed = http_consume(m->config.http, m->http_id, &completion);
    if (consumed == HTTP_PENDING) { return MEDIA_OK; }
    MediaSlot *s = &m->slots[m->active_slot]; m->active_slot = -1;
    if (s->stage == MS_RETIRED) { token_authorization_clear(&s->authorization); media_reap(m, s); return MEDIA_OK; }
    if (s->stage == MS_FAILED) { token_authorization_clear(&s->authorization); return MEDIA_OK; }
    if (consumed != HTTP_OK || !completion.final) { media_finish(s, MEDIA_HTTP_ERROR); return MEDIA_OK; }
    if (core_deadline_expired(core_clock_now(&m->config.clock), s->deadline)) {
        token_authorization_clear(&s->authorization); media_finish(s, MEDIA_TIMEOUT); return MEDIA_OK;
    }
    if (m->config.tokens != NULL && s->authorization.issued) {
        TokenResult token = token_report_status(m->config.tokens, &s->authorization, completion.status);
        if (token == TOKEN_RETRY) { m->needs_drive = true; return MEDIA_OK; }
        if (token != TOKEN_OK) { token_authorization_clear(&s->authorization); media_finish(s, MEDIA_AUTH); return MEDIA_OK; }
    }
    token_authorization_clear(&s->authorization);
    MediaResult result;
    if (completion.status == 401 || completion.status == 403) { result = MEDIA_AUTH; }
    else if (completion.result == HTTP_TIMEOUT) { result = MEDIA_TIMEOUT; }
    else if (completion.result == HTTP_BODY_TOO_LARGE) { result = MEDIA_LIMIT; }
    else if (completion.result != HTTP_OK || completion.status != 200) { result = MEDIA_HTTP_ERROR; }
    else { result = body_received(m, s, completion.bytes); }
    if (result != MEDIA_OK && result != MEDIA_PENDING) { media_finish(s, result); }
    return MEDIA_OK;
}

MediaResult media_http_start(MediaResolver *m, size_t index)
{
    MediaSlot *s = &m->slots[index]; size_t bytes = 0;
    bool metadata = s->stage == MS_METADATA;
    if (metadata) {
        if (swprintf_s(m->url, HTTP_URL_CHARS, L"%ls/extended-metadata/v0/extended-metadata", m->origin) < 0) { return MEDIA_LIMIT; }
        MediaResult result = metadata_request_body(s->request_uri, m->upload, sizeof(m->upload), &bytes);
        if (result != MEDIA_OK) { return result; }
    } else {
        MediaResult result = storage_resolve_url(m->origin, &s->source.track, m->url);
        if (result != MEDIA_OK) { return result; }
    }
    HttpHeader headers[5]; size_t count = 0;
    if (m->config.tokens != NULL && m->config.fixture_origin == NULL) {
        TokenResult result = token_authorize(m->config.tokens, m->url, &s->authorization);
        if (result == TOKEN_PENDING || result == TOKEN_FULL) { return MEDIA_PENDING; }
        if (result != TOKEN_OK) { token_authorization_clear(&s->authorization); return MEDIA_AUTH; }
        headers[count++] = s->authorization.headers[0]; headers[count++] = s->authorization.headers[1];
    }
    headers[count++] = (HttpHeader){L"Cache-Control", L"no-cache"};
    if (metadata) {
        headers[count++] = (HttpHeader){L"Content-Type", L"application/protobuf"};
        headers[count++] = (HttpHeader){L"Accept", L"application/protobuf"};
    }
    uint32_t timeout = core_deadline_wait(core_clock_now(&m->config.clock), s->deadline);
    if (timeout == 0) { return MEDIA_TIMEOUT; }
    HttpRequest request = {.url = m->url, .method = metadata ? HTTP_POST : HTTP_GET,
        .headers = headers, .header_count = count, .upload = bytes == 0 ? NULL : m->upload, .upload_bytes = bytes,
        .body = m->response, .body_capacity = sizeof(m->response), .body_limit = sizeof(m->response), .timeout_ms = timeout};
    HttpResult result = http_submit(m->config.http, &request, &m->http_id);
    if (result == HTTP_FULL) { return MEDIA_PENDING; }
    if (result != HTTP_OK) { token_authorization_clear(&s->authorization); return MEDIA_HTTP_ERROR; }
    m->active_slot = (int)index; return MEDIA_PENDING;
}

MediaResult media_source_poll(MediaResolver *m)
{
    if (m == NULL) { return MEDIA_INVALID; }
    if (m->busy) { return MEDIA_BAD_STATE; }
    m->busy = true; m->needs_drive = false;
    if (m->config.tokens != NULL) { (void)token_poll(m->config.tokens); }
    (void)audio_key_tick(m->config.keys);
    MediaResult result = media_http_poll(m);
    uint64_t now = core_clock_now(&m->config.clock);
    for (size_t i = 0; i < MEDIA_SOURCE_SLOTS; ++i) {
        MediaSlot *s = &m->slots[i];
        if (s->stage == MS_READY && m->config.http) {
            bool due = false;
            for (size_t j = 0; j < s->source.url_count; ++j) { due |= core_deadline_expired(now, s->source.urls[j].refresh_at); }
            if (due) { s->stage = MS_STORAGE; s->deadline = core_deadline_after(now, m->config.timeout_ms); }
        }
        if (s->stage != MS_METADATA && s->stage != MS_KEY && s->stage != MS_STORAGE) { continue; }
        if (core_deadline_expired(now, s->deadline)) {
            AudioKeyId key = s->key_request; s->key_request.value = 0;
            media_finish(s, MEDIA_TIMEOUT);
            if (key.value != 0) { (void)audio_key_cancel(m->config.keys, key); }
            if (m->active_slot == (int)i) { (void)http_cancel(m->config.http, m->http_id); }
            continue;
        }
        if (s->stage == MS_KEY && s->key_request.value == 0) {
            ApResult accepted = audio_key_request(m->config.keys, s->source.track.gid, s->source.track.file_id,
                s->deadline, media_key_completed, m, &s->key_request);
            if (accepted != AP_OK && accepted != AP_FULL) { media_finish(s, accepted == AP_TIMEOUT ? MEDIA_TIMEOUT : MEDIA_KEY_ERROR); }
        } else if ((s->stage == MS_METADATA || s->stage == MS_STORAGE) && m->active_slot < 0 && !m->shutdown) {
            MediaResult started = media_http_start(m, i);
            if (started != MEDIA_PENDING) { media_finish(s, started); }
        }
    }
    m->needs_drive = false; m->busy = false; return result;
}

uint32_t media_source_next_wait_ms(const MediaResolver *m)
{
    if (m == NULL) { return UINT32_MAX; }
    if (m->needs_drive && m->active_slot < 0) { return 0; }
    uint64_t deadline = CORE_CLOCK_NEVER;
    for (size_t i = 0; i < MEDIA_SOURCE_SLOTS; ++i) {
        const MediaSlot *s = &m->slots[i];
        if ((s->stage == MS_METADATA || s->stage == MS_KEY || s->stage == MS_STORAGE) && s->deadline < deadline) { deadline = s->deadline; }
        if (s->stage == MS_READY) {
            for (size_t j = 0; j < s->source.url_count; ++j) { if (s->source.urls[j].refresh_at < deadline) { deadline = s->source.urls[j].refresh_at; } }
        }
    }
    uint32_t wait = core_deadline_wait(core_clock_now(&m->config.clock), deadline);
    uint32_t child = audio_key_next_wait_ms(m->config.keys); if (child < wait) { wait = child; }
    child = http_next_wait_ms(m->config.http); if (child < wait) { wait = child; }
    if (m->config.tokens != NULL) { child = token_next_wait_ms(m->config.tokens); if (child < wait) { wait = child; } }
    return wait;
}
