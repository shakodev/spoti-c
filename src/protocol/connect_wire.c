#include "connect_internal.h"

static bool string_field(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
    const char *text = *arg;
    return pb_encode_tag_for_field(stream, field) && pb_encode_string(stream, (const pb_byte_t *)text, strlen(text));
}
static pb_callback_t string_callback(const char *text)
{
    pb_callback_t c = {0}; c.funcs.encode = string_field; c.arg = (void *)text; return c;
}
static bool next_tracks(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
    const PlayerQueueSnapshot *queue = *arg;
    for (unsigned i = queue->index + 1; i < queue->count; ++i) {
        spotify_connectstate_ProvidedTrack track = spotify_connectstate_ProvidedTrack_init_zero;
        connect_track_text(queue->ids[i], track.uri);
        if (!pb_encode_tag_for_field(stream, field) || !pb_encode_submessage(stream, spotify_connectstate_ProvidedTrack_fields, &track)) { return false; }
    }
    return true;
}
DealerResult connect_encode(ConnectState *s, void *out, size_t cap, size_t *written)
{
    if (!s || !out || !written || s->closing || s->logged_out) { return DEALER_INVALID; }
    spotify_connectstate_PutStateRequest request = spotify_connectstate_PutStateRequest_init_zero;
    PlayerQueueSnapshot queue; player_queue_snapshot(s->config.player, &queue); PlayerSnapshot p = queue.player;
    request.has_device = true; request.member_type = spotify_connectstate_MemberType_CONNECT_STATE;
    request.is_active = s->active; request.put_state_reason = s->registered ?
        spotify_connectstate_PutStateReason_PLAYER_STATE_CHANGED : spotify_connectstate_PutStateReason_NEW_CONNECTION;
    request.message_id = s->sequence; request.last_command_message_id = s->last_id;
    strcpy_s(request.last_command_sent_by_device_id, sizeof(request.last_command_sent_by_device_id), s->last_sender);
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    request.client_side_timestamp = ((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000;
    spotify_connectstate_DeviceInfo *info = &request.device.device_info;
    request.device.has_device_info = true; info->can_play = true; info->has_capabilities = true;
    info->volume = (uint32_t)(p.volume * 65535.0f); info->device_type = spotify_connectstate_devices_DeviceType_COMPUTER;
    strcpy_s(info->name, sizeof(info->name), s->config.name); strcpy_s(info->device_id, sizeof(info->device_id), s->config.device_id);
    strcpy_s(info->client_id, sizeof(info->client_id), s->config.client_id);
    strcpy_s(info->device_software_version, sizeof(info->device_software_version), "spoti-c/0.1.0");
    strcpy_s(info->spirc_version, sizeof(info->spirc_version), "3.2.6");
    spotify_connectstate_Capabilities *c = &info->capabilities;
    c->can_be_player = c->is_observable = c->is_controllable = c->supports_logout = true;
    c->command_acks = c->supports_transfer_command = c->supports_command_request = c->supports_gzip_pushes = true;
    c->volume_steps = 64; c->supported_types = string_callback("audio/track");
    /* All other capabilities deliberately remain false. */
    spotify_connectstate_PlayerState *state = &request.device.player_state;
    request.device.has_player_state = true; state->timestamp = (int64_t)request.client_side_timestamp;
    state->is_paused = p.state == PLAYER_PAUSED; state->is_playing = p.state == PLAYER_PLAYING || state->is_paused;
    state->is_buffering = p.state == PLAYER_BUFFERING || p.state == PLAYER_LOADING || state->is_paused;
    state->playback_speed = state->is_paused ? 0 : 1;
    state->position_as_of_timestamp = p.rate ? (int64_t)(p.position_frames / p.rate * 1000 + p.position_frames % p.rate * 1000 / p.rate) : 0;
    state->duration = p.rate ? (int64_t)(p.duration_frames / p.rate * 1000 + p.duration_frames % p.rate * 1000 / p.rate) : 0;
    state->next_tracks.funcs.encode = next_tracks; state->next_tracks.arg = &queue;
    bool any = false; for (unsigned i = 0; i < 16; ++i) { any |= p.track_id[i] != 0; }
    if (any) { state->has_track = true; connect_track_text(p.track_id, state->track.uri); }
    state->has_restrictions = true;
    state->restrictions.disallow_toggling_shuffle_reasons = string_callback("unsupported");
    state->restrictions.disallow_toggling_repeat_context_reasons = string_callback("unsupported");
    state->restrictions.disallow_toggling_repeat_track_reasons = string_callback("unsupported");
    state->restrictions.disallow_set_queue_reasons = string_callback("unsupported");
    state->restrictions.disallow_updating_context_reasons = string_callback("unsupported");
    return proto_encode(spotify_connectstate_PutStateRequest_fields, &request, out, cap, written) == PROTO_OK ? DEALER_OK : DEALER_LIMIT;
}
