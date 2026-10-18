#include "player_internal.h"

static unsigned audible_index(Player *p)
{
    return p->marker_count ? p->markers[p->marker_head].index : p->desired_index;
}
static void stop(Player *p)
{
    if (p->marker_count) { p->desired_index = p->markers[p->marker_head].index; }
    (void)sink_stop(p->config.sink); player_retire_all(p);
    p->desired = p->want_play = p->final_eof = false;
    p->marker_count = 0; p->view.state = PLAYER_STOPPED; p->view.position_frames = 0;
    p->view.timeline_valid = false;
    if (p->epoch != UINT64_MAX) { p->view.generation = ++p->epoch; }
}
static void source_reply(Player *p, const PlayerCommand *c)
{
    int at = c->ticket == p->slots[0].ticket ? 0 : c->ticket == p->slots[1].ticket ? 1 : -1;
    if (at < 0 || !c->ticket) { return; }
    PlayerSlot *s = &p->slots[at];
    if (c->type == PLAYER_SOURCE_RETIRED && s->state == PS_RETIRING && s->retirement_sent) {
        *s = (PlayerSlot){0}; player_ticket_publish(p, (unsigned)at);
    } else if (s->state == PS_REQUESTED && c->type == PLAYER_SOURCE_READY) {
        s->source = c->source; s->state = PS_READY;
    } else if (s->state == PS_REQUESTED && c->type == PLAYER_SOURCE_FAILED) {
        s->state = PS_FAILED; s->failure = c->error == AUDIO_OK ? AUDIO_CORRUPT : c->error;
    }
}
void player_command(Player *p, const PlayerCommand *c)
{
    if (c->type >= PLAYER_SOURCE_READY) { source_reply(p, c); return; }
    if (p->closing) { return; }
    switch (c->type) {
    case PLAYER_LOAD:
        stop(p); p->queue_count = 1; p->order[0] = 0; memcpy(p->ids[0], c->track_id, 16);
        p->want_play = c->play; player_begin(p, 0, c->position_frames, PLAYER_LOADING); break;
    case PLAYER_PLAY:
        p->want_play = true;
        if (p->view.state == PLAYER_PAUSED && p->current >= 0) { p->view.state = PLAYER_BUFFERING; }
        else if (p->queue_count && !p->desired && p->current < 0) {
            unsigned index = p->view.state == PLAYER_ENDED ? p->order[0] : p->desired_index;
            player_begin(p, index, 0, PLAYER_LOADING);
        }
        break;
    case PLAYER_PAUSE:
        if (p->queue_count && p->view.state != PLAYER_STOPPED && p->view.state != PLAYER_ENDED && p->view.state != PLAYER_ERROR) {
            p->want_play = false;
            player_begin(p, audible_index(p), p->view.position_frames, PLAYER_PAUSED);
        } break;
    case PLAYER_STOP: stop(p); break;
    case PLAYER_SEEK:
        if (p->queue_count && p->view.state != PLAYER_STOPPED) {
            player_begin(p, audible_index(p), c->position_frames, PLAYER_SEEKING);
        } break;
    case PLAYER_VOLUME:
        p->view.volume = c->volume < 0 ? 0 : c->volume > 1 ? 1 : c->volume;
        if (sink_set_volume(p->config.sink, p->view.volume) != AUDIO_OK) { player_fail(p, AUDIO_BAD_STATE); } break;
    case PLAYER_QUEUE_APPEND:
        if (p->queue_count < PLAYER_QUEUE_CAP) {
            unsigned i = p->queue_count++; p->order[i] = i; memcpy(p->ids[i], c->track_id, 16);
        } break;
    case PLAYER_QUEUE_CLEAR: {
        unsigned i = audible_index(p); uint64_t position = p->view.position_frames;
        if (p->queue_count) {
            uint8_t id[16]; memcpy(id, p->ids[i], 16);
            bool active = p->view.state != PLAYER_STOPPED && p->view.state != PLAYER_ENDED;
            if (active) { player_begin(p, i, position, p->want_play ? PLAYER_SEEKING : PLAYER_PAUSED); }
            memcpy(p->ids[0], id, 16); p->queue_count = 1; p->order[0] = 0; p->desired_index = 0;
        } break;
    }
    case PLAYER_NEXT: {
        unsigned next;
        if (player_next_index(p, audible_index(p), false, &next)) { player_begin(p, next, 0, PLAYER_LOADING); }
        else { stop(p); p->view.state = PLAYER_ENDED; } break;
    }
    case PLAYER_PREVIOUS: {
        if (!p->queue_count) { break; }
        unsigned index = audible_index(p), pos = 0;
        while (pos < p->queue_count && p->order[pos] != index) { ++pos; }
        if (p->view.position_frames < (uint64_t)p->view.rate * 3) {
            if (pos > 0 && pos <= p->queue_count) { index = p->order[pos - 1]; }
            else if (p->repeat == PLAYER_REPEAT_ALL) { index = p->order[p->queue_count - 1]; }
        }
        player_begin(p, index, 0, PLAYER_LOADING); break;
    }
    case PLAYER_REPEAT:
    case PLAYER_SHUFFLE: {
        unsigned index = audible_index(p); uint64_t position = p->view.position_frames;
        if (c->type == PLAYER_REPEAT) { p->repeat = (PlayerRepeat)c->value; }
        else { player_queue_shuffle(p, c->value); }
        if (p->queue_count && p->view.state != PLAYER_STOPPED && p->view.state != PLAYER_ENDED) {
            player_begin(p, index, position, p->want_play ? PLAYER_SEEKING : PLAYER_PAUSED);
        } break;
    }
    case PLAYER_SHUTDOWN: stop(p); p->closing = true; break;
    default: break;
    }
}
