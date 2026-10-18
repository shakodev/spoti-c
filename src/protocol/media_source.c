#include "media_internal.h"
#include <winhttp.h>
#include <wchar.h>

bool media_same_id(MediaId a, MediaId b)
{
    return a.generation == b.generation && a.slot == b.slot && a.reserved == 0 && b.reserved == 0;
}

MediaSlot *media_slot(MediaResolver *m, MediaId id)
{
    if (id.slot >= MEDIA_SOURCE_SLOTS || id.generation == 0 || id.reserved != 0) { return NULL; }
    MediaSlot *s = &m->slots[id.slot];
    return s->stage != MS_FREE && media_same_id(s->id, id) ? s : NULL;
}

static bool origin_valid(const wchar_t *origin, bool fixture)
{
    if (origin == NULL || wcsnlen_s(origin, 256) == 256) { return false; }
    URL_COMPONENTS u = {0}; u.dwStructSize = sizeof(u);
    u.dwHostNameLength = u.dwUrlPathLength = u.dwExtraInfoLength = (DWORD)-1;
    u.dwUserNameLength = u.dwPasswordLength = (DWORD)-1;
    if (!WinHttpCrackUrl(origin, 0, 0, &u) || u.dwHostNameLength == 0 || u.dwUrlPathLength != 0 ||
        u.dwExtraInfoLength != 0 || u.dwUserNameLength != 0 || u.dwPasswordLength != 0) { return false; }
    if (fixture) { return u.nScheme == INTERNET_SCHEME_HTTP && u.dwHostNameLength == 9 && wcsncmp(u.lpszHostName, L"127.0.0.1", 9) == 0; }
    return u.nScheme == INTERNET_SCHEME_HTTPS;
}

MediaResult media_resolver_create(Arena *arena, const MediaConfig *config, MediaResolver **out)
{
    if (out == NULL) { return MEDIA_INVALID; } *out = NULL;
    if (arena == NULL || config == NULL || config->http == NULL || config->keys == NULL ||
        config->policy.catalogue == NULL || strnlen_s(config->policy.catalogue, 33) == 0 ||
        strnlen_s(config->policy.catalogue, 33) == 33 || config->policy.country[0] < 'A' || config->policy.country[0] > 'Z' ||
        config->policy.country[1] < 'A' || config->policy.country[1] > 'Z' || config->policy.country[2] != 0) { return MEDIA_INVALID; }
    bool fixture = config->fixture_origin != NULL;
    const wchar_t *origin = fixture ? config->fixture_origin : config->spclient_origin;
    if ((!fixture && config->tokens == NULL) || !origin_valid(origin, fixture)) { return MEDIA_INVALID; }
    MediaResolver *m = arena_alloc(arena, sizeof(*m), _Alignof(MediaResolver));
    if (m == NULL) { return MEDIA_FULL; }
    memset(m, 0, sizeof(*m)); m->config = *config; m->active_slot = -1;
    if (m->config.timeout_ms == 0) { m->config.timeout_ms = 15000; }
    wcscpy_s(m->origin, 256, origin); strcpy_s(m->catalogue, sizeof(m->catalogue), config->policy.catalogue);
    m->config.policy.catalogue = m->catalogue; *out = m; return MEDIA_OK;
}

uint64_t media_utc(const MediaResolver *m)
{
    if (m->config.utc_ms != NULL) { return m->config.utc_ms(m->config.utc_context); }
    FILETIME time; GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER value; value.LowPart = time.dwLowDateTime; value.HighPart = time.dwHighDateTime;
    return value.QuadPart / 10000 - UINT64_C(11644473600000);
}

void media_reap(MediaResolver *m, MediaSlot *s)
{
    if (s->stage != MS_RETIRED || s->leases != 0) { return; }
    SecureZeroMemory(s->secret, sizeof(s->secret));
    if (m->active_slot != (int)s->id.slot) { SecureZeroMemory(s, sizeof(*s)); }
}

void media_finish(MediaSlot *s, MediaResult result)
{
    token_authorization_clear(&s->authorization);
    s->result = result; s->stage = result == MEDIA_OK ? MS_READY : MS_FAILED;
    if (result != MEDIA_OK && s->leases == 0) { SecureZeroMemory(s->secret, sizeof(s->secret)); }
    if (!s->notified) { s->notified = true; s->callback(s->context, s->id, result); }
}

MediaResult media_source_resolve(MediaResolver *m, const char *uri, MediaCallback callback, void *context, MediaId *out)
{
    if (m == NULL || callback == NULL || out == NULL) { return MEDIA_INVALID; }
    if (m->busy) { return MEDIA_BAD_STATE; }
    if (m->shutdown) { return MEDIA_CLOSED; }
    uint8_t gid[16]; MediaResult result = media_gid_from_uri(uri, gid);
    if (result != MEDIA_OK) { return result; }
    if (m->generation == UINT64_MAX) { return MEDIA_EXHAUSTED; }
    for (size_t i = 0; i < MEDIA_SOURCE_SLOTS; ++i) {
        MediaSlot *s = &m->slots[i]; if (s->stage != MS_FREE) { continue; }
        memset(s, 0, sizeof(*s)); s->stage = MS_METADATA; s->id = (MediaId){++m->generation, (uint32_t)i, 0};
        s->source.key = s->id; memcpy(s->source.original_uri, uri, MEDIA_URI_BYTES);
        memcpy(s->request_uri, uri, MEDIA_URI_BYTES); memcpy(s->candidates[0].gid, gid, 16); s->candidate_count = 1;
        s->callback = callback; s->context = context; s->result = MEDIA_PENDING;
        s->deadline = core_deadline_after(core_clock_now(&m->config.clock), m->config.timeout_ms);
        *out = s->id; m->needs_drive = true; return MEDIA_OK;
    }
    return MEDIA_FULL;
}

void media_key_completed(void *context, AudioKeyId id, ApResult result, const uint8_t key[16])
{
    MediaResolver *m = context; bool was_busy = m->busy; m->busy = true;
    for (size_t i = 0; i < MEDIA_SOURCE_SLOTS; ++i) {
        MediaSlot *s = &m->slots[i];
        if (s->stage != MS_KEY || s->key_request.value != id.value) { continue; }
        s->key_request.value = 0;
        if (result == AP_OK && key != NULL) { memcpy(s->secret, key, 16); s->stage = MS_STORAGE; m->needs_drive = true; }
        else { media_finish(s, result == AP_TIMEOUT ? MEDIA_TIMEOUT : MEDIA_KEY_ERROR); }
        break;
    }
    m->busy = was_busy;
}

MediaResult media_source_get(MediaResolver *m, MediaId id, MediaSource *out)
{
    if (m == NULL || out == NULL) { return MEDIA_INVALID; }
    MediaSlot *s = media_slot(m, id);
    if (s == NULL || s->stage == MS_RETIRED) { return MEDIA_STALE; }
    if (s->stage == MS_FAILED) { return s->result; }
    if (s->stage != MS_READY && !(s->stage == MS_STORAGE && s->notified && s->result == MEDIA_OK)) { return MEDIA_PENDING; }
    uint64_t now = core_clock_now(&m->config.clock); MediaSource copy = s->source; copy.url_count = 0;
    for (size_t i = 0; i < s->source.url_count; ++i) {
        if (!core_deadline_expired(now, s->source.urls[i].refresh_at)) { copy.urls[copy.url_count++] = s->source.urls[i]; }
    }
    if (copy.url_count == 0) { return MEDIA_EXPIRED; }
    *out = copy; return MEDIA_OK;
}

static void retire(MediaResolver *m, MediaSlot *s)
{
    bool notify = !s->notified; s->notified = true; s->stage = MS_RETIRED;
    token_authorization_clear(&s->authorization);
    if (s->key_request.value != 0) { (void)audio_key_cancel(m->config.keys, s->key_request); s->key_request.value = 0; }
    if (m->active_slot == (int)s->id.slot) { (void)http_cancel(m->config.http, m->http_id); }
    if (notify) { s->callback(s->context, s->id, MEDIA_CANCELLED); }
    media_reap(m, s);
}

MediaResult media_source_retire(MediaResolver *m, MediaId id)
{
    if (m == NULL) { return MEDIA_INVALID; }
    if (m->busy) { return MEDIA_BAD_STATE; }
    MediaSlot *s = media_slot(m, id); if (s == NULL) { return MEDIA_STALE; }
    m->busy = true; retire(m, s); m->busy = false; return MEDIA_OK;
}

MediaResult media_key_acquire(MediaResolver *m, MediaKeyHandle handle, MediaKeyLease *lease)
{
    if (m == NULL || lease == NULL || lease->ticket != 0) { return MEDIA_INVALID; }
    if (m->busy) { return MEDIA_BAD_STATE; }
    MediaSlot *s = media_slot(m, handle);
    if (s == NULL || m->shutdown ||
        (s->stage != MS_READY && !(s->stage == MS_STORAGE && s->notified && s->result == MEDIA_OK))) { return MEDIA_STALE; }
    if (m->ticket == UINT64_MAX) { return MEDIA_EXHAUSTED; }
    for (size_t i = 0; i < MEDIA_LEASES; ++i) {
        if (m->leases[i].ticket != 0) { continue; }
        m->leases[i] = (MediaTicket){++m->ticket, handle}; ++s->leases;
        *lease = (MediaKeyLease){handle, s->secret, m->ticket}; return MEDIA_OK;
    }
    return MEDIA_FULL;
}

MediaResult media_key_release(MediaResolver *m, MediaKeyLease *lease)
{
    if (m == NULL || lease == NULL) { return MEDIA_INVALID; }
    if (m->busy) { return MEDIA_BAD_STATE; }
    MediaSlot *s = media_slot(m, lease->handle);
    if (s == NULL || s->leases == 0 || lease->key != s->secret || lease->ticket == 0) { return MEDIA_STALE; }
    for (size_t i = 0; i < MEDIA_LEASES; ++i) {
        MediaTicket *t = &m->leases[i];
        if (t->ticket != lease->ticket || !media_same_id(t->handle, lease->handle)) { continue; }
        memset(t, 0, sizeof(*t)); memset(lease, 0, sizeof(*lease)); --s->leases;
        if (s->leases == 0 && s->stage == MS_FAILED) { SecureZeroMemory(s->secret, sizeof(s->secret)); }
        media_reap(m, s); return MEDIA_OK;
    }
    return MEDIA_STALE;
}

MediaResult media_resolver_shutdown(MediaResolver *m)
{
    if (m == NULL) { return MEDIA_INVALID; }
    if (m->busy) { return MEDIA_BAD_STATE; }
    m->busy = m->shutdown = true;
    for (size_t i = 0; i < MEDIA_SOURCE_SLOTS; ++i) { if (m->slots[i].stage != MS_FREE) { retire(m, &m->slots[i]); } }
    (void)media_http_poll(m);
    bool balanced = media_resolver_balanced(m); m->busy = false;
    if (balanced) { SecureZeroMemory(m->response, sizeof(m->response)); SecureZeroMemory(m->upload, sizeof(m->upload)); }
    return balanced ? MEDIA_OK : MEDIA_PENDING;
}

bool media_resolver_balanced(const MediaResolver *m)
{
    if (m == NULL || m->active_slot >= 0) { return false; }
    for (size_t i = 0; i < MEDIA_SOURCE_SLOTS; ++i) { if (m->slots[i].stage != MS_FREE) { return false; } }
    for (size_t i = 0; i < MEDIA_LEASES; ++i) { if (m->leases[i].ticket != 0) { return false; } }
    return true;
}
