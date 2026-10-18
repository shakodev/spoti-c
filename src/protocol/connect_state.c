#include "connect_internal.h"
#include <math.h>

static bool text(const char *p, size_t cap, bool identifier)
{
    size_t n = p ? strnlen_s(p, cap) : 0;
    if (!n || n == cap) { return false; }
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c < 32 || c == 127 || (identifier && !((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))) { return false; }
    }
    return true;
}
DealerResult connect_create(Arena *arena, const ConnectConfig *cfg, ConnectState **out)
{
    if (!out) { return DEALER_INVALID; } *out = NULL;
    if (!arena || arena->capacity > MEMORY_BUDGET_CORE || !cfg || !cfg->player || !text(cfg->device_id, 257, true) ||
        !text(cfg->name, 257, false) || !text(cfg->client_id, 257, true) ||
        (!cfg->fixture_replay && (!cfg->http || (!cfg->fixture_http_origin && (!cfg->tokens || !cfg->spclient_origin))))) { return DEALER_INVALID; }
    ConnectState *s = arena_alloc(arena, sizeof(*s), _Alignof(ConnectState)); if (!s) { return DEALER_FULL; }
    memset(s, 0, sizeof(*s)); s->config = *cfg; s->cluster_timestamp = -1;
    InitializeSRWLock(&s->snapshot_lock); connect_publish(s); *out = s; return DEALER_OK;
}
void connect_publish(ConnectState *s)
{
    ConnectSnapshot view = {.player = player_snapshot(s->config.player), .revision = s->revision,
        .registered = s->registered, .active = s->active, .logged_out = s->logged_out, .last_message_id = s->last_id, .controls = s->controls};
    strcpy_s(view.active_device_id, sizeof(view.active_device_id), s->active_device_id);
    AcquireSRWLockExclusive(&s->snapshot_lock); s->published = view; ReleaseSRWLockExclusive(&s->snapshot_lock);
}
ConnectSnapshot connect_snapshot(ConnectState *s)
{
    ConnectSnapshot view = {0}; if (!s) { return view; }
    AcquireSRWLockShared(&s->snapshot_lock); view = s->published; ReleaseSRWLockShared(&s->snapshot_lock); return view;
}
void connect_changed(ConnectState *s)
{
    if (s && !s->closing && !s->logged_out) { s->dirty = true; if (s->revision != UINT64_MAX) { ++s->revision; } connect_publish(s); }
}
DealerResult connect_connection(ConnectState *s, const char *id)
{
    if (!s || s->closing || s->logged_out) { return DEALER_CLOSED; }
    if (!id || !*id) {
        ++s->connection_epoch;
        s->registered = false; s->connection_id[0] = 0; connect_publish(s); return DEALER_OK;
    }
    if (!text(id, 257, false)) { return DEALER_INVALID; }
    strcpy_s(s->connection_id, sizeof(s->connection_id), id);
    ++s->connection_epoch;
    s->registered = s->config.fixture_replay; connect_changed(s); return DEALER_OK;
}
DealerResult connect_message(ConnectState *s, const char *uri, const void *data, size_t n)
{
    if (!s || !uri || s->closing || s->logged_out) { return DEALER_CLOSED; }
    if (!strcmp(uri, "hm://connect-state/v1/cluster")) {
        spotify_connectstate_ClusterUpdate update = spotify_connectstate_ClusterUpdate_init_zero;
        if (proto_decode(spotify_connectstate_ClusterUpdate_fields, data, n, &update) != PROTO_OK || !update.has_cluster) {
            return DEALER_MALFORMED;
        }
        return connect_cluster(s, &update.cluster);
    }
    if (!strcmp(uri, "hm://connect-state/v1/connect/volume")) {
        spotify_connectstate_SetVolumeCommand volume = spotify_connectstate_SetVolumeCommand_init_zero;
        if (proto_decode(spotify_connectstate_SetVolumeCommand_fields, data, n, &volume) != PROTO_OK || volume.volume < 0 || volume.volume > 65535) {
            return DEALER_MALFORMED;
        }
        if (volume.has_command_options) {
            if (volume.command_options.message_id < 0) { return DEALER_MALFORMED; }
            for (unsigned i = 0; i < CONNECT_DEDUP; ++i) {
                if (s->seen[i].used && s->seen[i].volume && s->seen[i].id == (uint32_t)volume.command_options.message_id) { return s->seen[i].result; }
            }
        }
        PlayerCommand c = {.type = PLAYER_VOLUME, .volume = (float)volume.volume / 65535.0f};
        if (player_submit(s->config.player, &c) != PLAYER_OK) { return DEALER_FULL; }
        s->controls |= 1u << 6;
        if (volume.has_command_options) {
            s->seen[s->cursor++ % CONNECT_DEDUP] = (ConnectSeen){.id = (uint32_t)volume.command_options.message_id,
                .used = true, .volume = true, .result = DEALER_OK};
        }
        connect_changed(s); return DEALER_OK;
    }
    if (!strcmp(uri, "hm://connect-state/v1/connect/logout")) {
        spotify_connectstate_LogoutCommand logout = spotify_connectstate_LogoutCommand_init_zero;
        if (proto_decode(spotify_connectstate_LogoutCommand_fields, data, n, &logout) != PROTO_OK) { return DEALER_MALFORMED; }
        if (player_submit(s->config.player, &(PlayerCommand){.type = PLAYER_STOP}) != PLAYER_OK) { return DEALER_FULL; }
        s->logged_out = true; s->active = s->registered = false; s->dirty = false;
        SecureZeroMemory(s->seen, sizeof(s->seen)); connect_publish(s); return DEALER_CLOSED;
    }
    return DEALER_UNSUPPORTED;
}
DealerResult connect_cluster(ConnectState *s, const spotify_connectstate_Cluster *cluster)
{
    if (cluster->changed_timestamp_ms < s->cluster_timestamp) { return DEALER_STALE; }
    bool active = !strcmp(cluster->active_device_id, s->config.device_id);
    if (s->active && !active && player_submit(s->config.player, &(PlayerCommand){.type = PLAYER_PAUSE}) != PLAYER_OK) { return DEALER_FULL; }
    s->cluster_timestamp = cluster->changed_timestamp_ms;
    strcpy_s(s->active_device_id, sizeof(s->active_device_id), cluster->active_device_id);
    s->active = active; connect_publish(s); return DEALER_OK;
}
DealerResult connect_shutdown(ConnectState *s)
{
    if (!s) { return DEALER_INVALID; } s->closing = true;
    if (s->pending) {
        (void)http_cancel(s->config.http, s->request); (void)http_pump(s->config.http); HttpCompletion completion;
        HttpResult r = http_consume(s->config.http, s->request, &completion);
        if (r == HTTP_PENDING) { return DEALER_PENDING; } s->pending = false;
    }
    token_authorization_clear(&s->auth); SecureZeroMemory(s->upload, sizeof(s->upload));
    SecureZeroMemory(s->response, sizeof(s->response));
    s->registered = false; connect_publish(s); return DEALER_OK;
}
