#include "connect_internal.h"

bool connect_track_uri(const char *uri, uint8_t out[16])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (!uri || strlen(uri) != 36 || strncmp(uri, "spotify:track:", 14)) { return false; }
    uint8_t id[16] = {0}; unsigned any = 0;
    for (unsigned i = 14; i < 36; ++i) {
        const char *p = strchr(alphabet, uri[i]); if (!p) { return false; }
        unsigned carry = (unsigned)(p - alphabet);
        for (unsigned j = 16; j-- > 0;) { carry += id[j] * 62u; id[j] = (uint8_t)carry; carry >>= 8; }
        if (carry) { return false; }
    }
    for (unsigned i = 0; i < 16; ++i) { any |= id[i]; }
    if (!any) { return false; } memcpy(out, id, 16); return true;
}
void connect_track_text(const uint8_t id[16], char out[37])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    uint8_t copy[16]; memcpy(copy, id, 16); memcpy(out, "spotify:track:", 14); out[36] = 0;
    for (unsigned i = 36; i-- > 14;) {
        unsigned rem = 0;
        for (unsigned j = 0; j < 16; ++j) { unsigned n = rem * 256 + copy[j]; copy[j] = (uint8_t)(n / 62); rem = n % 62; }
        out[i] = alphabet[rem];
    }
}
static bool track_id(const spotify_player_proto_ContextTrack *track, uint8_t id[16])
{
    if (track->has_uri) { return connect_track_uri(track->uri, id); }
    if (!track->has_gid || track->gid.size != 16) { return false; }
    unsigned any = 0;
    for (unsigned i = 0; i < 16; ++i) { any |= track->gid.bytes[i]; }
    if (!any) { return false; } memcpy(id, track->gid.bytes, 16); return true;
}
static bool queue_track(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void)field; PlayerTransfer *queue = *arg;
    spotify_player_proto_ContextTrack track = spotify_player_proto_ContextTrack_init_zero;
    if (queue->count == PLAYER_QUEUE_CAP || !pb_decode(stream, spotify_player_proto_ContextTrack_fields, &track) ||
        !track_id(&track, queue->ids[queue->count])) { return false; }
    ++queue->count; return true;
}
typedef struct TransferContext {
    PlayerTransfer tracks;
    char uids[PLAYER_QUEUE_CAP][257];
    bool unsupported;
} TransferContext;
static bool context_track(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void)field; TransferContext *ctx = *arg;
    spotify_player_proto_ContextTrack track = spotify_player_proto_ContextTrack_init_zero;
    if (ctx->tracks.count == PLAYER_QUEUE_CAP || !pb_decode(stream, spotify_player_proto_ContextTrack_fields, &track) ||
        !track_id(&track, ctx->tracks.ids[ctx->tracks.count])) { return false; }
    strcpy_s(ctx->uids[ctx->tracks.count], 257, track.uid); ++ctx->tracks.count; return true;
}
static bool context_page(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void)field; TransferContext *ctx = *arg;
    spotify_player_proto_ContextPage page = spotify_player_proto_ContextPage_init_zero;
    page.tracks.funcs.decode = context_track; page.tracks.arg = ctx;
    if (!pb_decode(stream, spotify_player_proto_ContextPage_fields, &page)) { return false; }
    ctx->unsupported |= page.loading || page.next_page_url[0] != 0; return true;
}
static bool unsupported_field(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void)field; *(bool *)*arg = true; return pb_read(stream, NULL, stream->bytes_left);
}
DealerResult connect_transfer(const void *data, size_t n, PlayerTransfer *queue)
{
    spotify_player_proto_transfer_TransferState state = spotify_player_proto_transfer_TransferState_init_zero;
    PlayerTransfer transfer = {0};
    TransferContext context = {0};
    state.current_session.context.pages.funcs.decode = context_page;
    state.current_session.context.pages.arg = &context;
    state.options.modes.funcs.decode = unsupported_field; state.options.modes.arg = &context.unsupported;
    state.current_session.option_overrides.modes = state.options.modes;
    state.queue.tracks.funcs.decode = queue_track; state.queue.tracks.arg = &transfer;
    if (proto_decode(spotify_player_proto_transfer_TransferState_fields, data, n, &state) != PROTO_OK ||
        !state.has_playback ||
        state.playback.position_as_of_timestamp < 0) { return DEALER_MALFORMED; }
    if (state.queue.is_playing_queue) {
        if (!transfer.count) { return DEALER_MALFORMED; }
    } else {
        if (transfer.count == PLAYER_QUEUE_CAP) { return DEALER_LIMIT; }
        memmove(transfer.ids[1], transfer.ids[0], (size_t)transfer.count * 16); ++transfer.count;
        if (!state.playback.has_current_track || !track_id(&state.playback.current_track, transfer.ids[0])) { return DEALER_MALFORMED; }
    }
    spotify_player_proto_transfer_Session *session = &state.current_session;
    if (context.unsupported || session->context.loading || session->shuffle_seed[0] ||
        session->option_overrides.shuffling_context || session->option_overrides.repeating_context || session->option_overrides.repeating_track ||
        (session->option_overrides.has_playback_speed && session->option_overrides.playback_speed != 1.0f) ||
        (state.has_options && (state.options.shuffling_context || state.options.repeating_track || state.options.repeating_context)) ||
        (state.has_options && state.options.has_playback_speed && state.options.playback_speed != 1.0f) ||
        (state.playback.has_playback_speed && state.playback.playback_speed != 1.0 &&
            !(state.playback.is_paused && state.playback.playback_speed == 0.0))) { return DEALER_UNSUPPORTED; }
    if (context.tracks.count) {
        if (state.queue.is_playing_queue) { return DEALER_UNSUPPORTED; }
        unsigned at = 0;
        while (at < context.tracks.count && (session->current_uid[0] ?
            strcmp(context.uids[at], session->current_uid) != 0 : memcmp(context.tracks.ids[at], transfer.ids[0], 16) != 0)) { ++at; }
        if (at == context.tracks.count) { return DEALER_UNSUPPORTED; }
        while (++at < context.tracks.count) {
            if (transfer.count == PLAYER_QUEUE_CAP) { return DEALER_LIMIT; }
            memcpy(transfer.ids[transfer.count++], context.tracks.ids[at], 16);
        }
    } else if (session->has_context && session->context.uri[0] && strcmp(session->context.uri, "-") &&
        strcmp(session->context.uri, "spotify:unknown") && strncmp(session->context.uri, "spotify:track:", 14)) {
        return DEALER_UNSUPPORTED;
    }
    transfer.position_ms = (uint32_t)state.playback.position_as_of_timestamp;
    transfer.play = !state.playback.is_paused; *queue = transfer; return DEALER_OK;
}
