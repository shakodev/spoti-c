#include "connect_internal.h"

static DealerResult play(yyjson_val *cmd, PlayerCommand *c, PlayerTransfer *transfer)
{
    yyjson_val *context = yyjson_obj_get(cmd, "context"), *options = yyjson_obj_get(cmd, "options");
    char uri[257]; c->type = PLAYER_TRANSFER; transfer->count = 1; transfer->play = true;
    yyjson_val *context_uri = yyjson_obj_get(context, "uri"), *pages = yyjson_obj_get(context, "pages");
    if (context_uri && (!dealer_string(context_uri, uri, sizeof(uri)) || strncmp(uri, "spotify:track:", 14))) { return DEALER_UNSUPPORTED; }
    if (pages && (!yyjson_is_arr(pages) || yyjson_arr_size(pages))) { return DEALER_UNSUPPORTED; }
    yyjson_val *track = yyjson_obj_get(yyjson_obj_get(options, "skip_to"), "track_uri");
    if (!track) { track = yyjson_obj_get(context, "uri"); }
    if (!dealer_string(track, uri, sizeof(uri)) || !connect_track_uri(uri, transfer->ids[0])) { return DEALER_UNSUPPORTED; }
    yyjson_val *override = yyjson_obj_get(options, "player_options_override");
    if (override && !yyjson_is_null(override)) { return DEALER_UNSUPPORTED; }
    yyjson_val *v = yyjson_obj_get(options, "initially_paused");
    if (v) { if (!yyjson_is_bool(v)) { return DEALER_MALFORMED; } transfer->play = !yyjson_get_bool(v); }
    v = yyjson_obj_get(options, "seek_to");
    if (v && !dealer_u32(v, INT32_MAX, &transfer->position_ms)) { return DEALER_MALFORMED; }
    return DEALER_OK;
}
static DealerResult parse_command(ConnectState *s, yyjson_val *cmd, PlayerCommand *c, PlayerTransfer *transfer)
{
    char endpoint[64];
    if (!dealer_string(yyjson_obj_get(cmd, "endpoint"), endpoint, sizeof(endpoint))) { return DEALER_MALFORMED; }
    if (!strcmp(endpoint, "resume")) { c->type = PLAYER_PLAY; }
    else if (!strcmp(endpoint, "pause")) { c->type = PLAYER_PAUSE; }
    else if (!strcmp(endpoint, "skip_next")) {
        if (yyjson_obj_get(cmd, "track") && !yyjson_is_null(yyjson_obj_get(cmd, "track"))) { return DEALER_UNSUPPORTED; }
        c->type = PLAYER_NEXT;
    } else if (!strcmp(endpoint, "skip_prev")) { c->type = PLAYER_PREVIOUS; }
    else if (!strcmp(endpoint, "seek_to")) {
        uint32_t ms; PlayerSnapshot snapshot = player_snapshot(s->config.player);
        if (!snapshot.rate || !dealer_u32(yyjson_obj_get(cmd, "value"), INT32_MAX, &ms)) { return DEALER_MALFORMED; }
        c->type = PLAYER_SEEK; c->seek_ms = true; c->position_ms = ms;
    } else if (!strcmp(endpoint, "play")) { return play(cmd, c, transfer); }
    else if (!strcmp(endpoint, "transfer")) {
        yyjson_val *options = yyjson_obj_get(cmd, "options");
        if (options && (!yyjson_is_obj(options) || yyjson_obj_size(options))) { return DEALER_UNSUPPORTED; }
        yyjson_val *data = yyjson_obj_get(cmd, "data");
        if (!yyjson_is_str(data)) { return DEALER_UNSUPPORTED; }
        char *bytes = (char *)yyjson_get_str(data); size_t n;
        DealerResult r = dealer_base64(bytes, yyjson_get_len(data), bytes, DEALER_PAYLOAD_CAP, &n);
        c->type = PLAYER_TRANSFER; return r == DEALER_OK ? connect_transfer(bytes, n, transfer) : r;
    } else { return DEALER_UNSUPPORTED; }
    return DEALER_OK;
}
DealerResult connect_command(ConnectState *s, char *data, size_t n, void *scratch)
{
    if (!s || s->closing || s->logged_out) { return DEALER_CLOSED; }
    if (!s->registered) { return DEALER_PENDING; }
    yyjson_doc *doc = dealer_json(data, n, scratch, DEALER_PAYLOAD_CAP);
    if (!doc) { return DEALER_MALFORMED; }
    yyjson_val *root = yyjson_doc_get_root(doc); char sender[257]; uint32_t id;
    PlayerTransfer transfer = {0};
    DealerResult result = DEALER_MALFORMED; PlayerCommand command = {.transfer = &transfer};
    if (!dealer_string(yyjson_obj_get(root, "sent_by_device_id"), sender, sizeof(sender)) || !*sender ||
        !dealer_u32(yyjson_obj_get(root, "message_id"), UINT32_MAX, &id)) { goto done; }
    for (unsigned i = 0; i < CONNECT_DEDUP; ++i) {
        if (s->seen[i].used && !s->seen[i].volume && s->seen[i].id == id && !strcmp(s->seen[i].sender, sender)) {
            result = s->seen[i].result; goto done;
        }
    }
    result = parse_command(s, yyjson_obj_get(root, "command"), &command, &transfer);
    if (result == DEALER_OK) {
        PlayerResult p = player_submit(s->config.player, &command);
        result = p == PLAYER_OK ? DEALER_OK : p == PLAYER_FULL ? DEALER_FULL : DEALER_CLOSED;
    }
    if (result != DEALER_FULL && result != DEALER_CLOSED) {
        ConnectSeen *seen = &s->seen[s->cursor++ % CONNECT_DEDUP];
        *seen = (ConnectSeen){.id = id, .used = true, .result = result}; strcpy_s(seen->sender, sizeof(seen->sender), sender);
        if (result == DEALER_OK) {
            unsigned bit = command.type == PLAYER_PLAY ? 0 : command.type == PLAYER_PAUSE ? 1 : command.type == PLAYER_SEEK ? 2 :
                command.type == PLAYER_NEXT ? 3 : command.type == PLAYER_PREVIOUS ? 4 : 5;
            if (command.type == PLAYER_TRANSFER && strcmp(yyjson_get_str(yyjson_obj_get(yyjson_obj_get(root, "command"), "endpoint")), "transfer")) { bit = 0; }
            s->controls |= 1u << bit;
            s->last_id = id; strcpy_s(s->last_sender, sizeof(s->last_sender), sender);
            if (command.type == PLAYER_TRANSFER || command.type == PLAYER_PLAY) {
                s->active = true; strcpy_s(s->active_device_id, sizeof(s->active_device_id), s->config.device_id);
            }
            connect_changed(s);
        }
    }
done:
    yyjson_doc_free(doc); return result;
}
