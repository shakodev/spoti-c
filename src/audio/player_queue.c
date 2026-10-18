#include "player_internal.h"
#include <math.h>

bool player_owner(const Player *p)
{
    return p && !p->destroyed && p->owner == GetCurrentThreadId();
}
bool player_id_valid(const uint8_t *id)
{
    unsigned any = 0; for (size_t i = 0; i < 16; ++i) { any |= id[i]; } return any != 0;
}
void player_ticket_publish(Player *p, unsigned slot)
{
    AcquireSRWLockExclusive(&p->lock);
    p->live_ticket[slot] = p->slots[slot].ticket;
    p->retiring[slot] = p->slots[slot].state == PS_RETIRING;
    ReleaseSRWLockExclusive(&p->lock);
}
PlayerResult player_submit(Player *p, const PlayerCommand *c)
{
    if (!p || !c || c->type < PLAYER_LOAD || c->type > PLAYER_DATA_READY ||
        ((c->type == PLAYER_LOAD || c->type == PLAYER_QUEUE_APPEND) && !player_id_valid(c->track_id)) ||
        (c->type == PLAYER_VOLUME && !isfinite(c->volume)) ||
        (c->type == PLAYER_REPEAT && c->value > PLAYER_REPEAT_ALL) ||
        (c->type == PLAYER_TRANSFER && (!c->transfer || !c->transfer->count || c->transfer->count > PLAYER_QUEUE_CAP)) ||
        (c->type == PLAYER_SOURCE_READY && (!c->source.read_at || c->source.bytes == 0 || c->source.bytes > UINT32_MAX))) {
        return PLAYER_INVALID;
    }
    if (c->type == PLAYER_TRANSFER) {
        for (uint32_t i = 0; i < c->transfer->count; ++i) {
            if (!player_id_valid(c->transfer->ids[i])) { return PLAYER_INVALID; }
        }
    }
    PlayerResult result = PLAYER_OK;
    AcquireSRWLockExclusive(&p->lock);
    if (p->destroyed || (p->control_closed && c->type < PLAYER_SOURCE_READY)) { result = PLAYER_CLOSED; }
    else if (c->type >= PLAYER_SOURCE_READY) {
        int slot = c->ticket != 0 && c->ticket == p->live_ticket[0] ? 0 :
            c->ticket != 0 && c->ticket == p->live_ticket[1] ? 1 : -1;
        if (slot < 0 || (p->retiring[slot] != (c->type == PLAYER_SOURCE_RETIRED))) { result = PLAYER_STALE; }
    }
    if (result == PLAYER_OK) {
        if (p->command_count == PLAYER_COMMAND_CAP || (c->type == PLAYER_TRANSFER && p->transfer_pending)) { result = PLAYER_FULL; }
        else if (c->type == PLAYER_QUEUE_APPEND && p->queue_admitted == PLAYER_QUEUE_CAP) { result = PLAYER_FULL; }
        else {
            p->commands[(p->command_head + p->command_count) % PLAYER_COMMAND_CAP] = *c;
            if (c->type == PLAYER_TRANSFER) {
                p->transfer = *c->transfer; p->transfer_pending = true;
                p->commands[(p->command_head + p->command_count) % PLAYER_COMMAND_CAP].transfer = &p->transfer;
            }
            ++p->command_count;
            if (c->type == PLAYER_SHUTDOWN) { p->control_closed = true; }
            if (c->type == PLAYER_LOAD) { p->queue_admitted = 1; }
            if (c->type == PLAYER_TRANSFER) { p->queue_admitted = c->transfer->count; }
            if (c->type == PLAYER_QUEUE_CLEAR && p->queue_admitted) { p->queue_admitted = 1; }
            if (c->type == PLAYER_QUEUE_APPEND) { ++p->queue_admitted; }
            SetEvent(p->wake);
        }
    }
    ReleaseSRWLockExclusive(&p->lock); return result;
}
bool player_pop_command(Player *p, PlayerCommand *c)
{
    AcquireSRWLockExclusive(&p->lock);
    bool have = p->command_count != 0;
    if (have) {
        *c = p->commands[p->command_head]; p->command_head = (p->command_head + 1) % PLAYER_COMMAND_CAP;
        --p->command_count;
    }
    ReleaseSRWLockExclusive(&p->lock); return have;
}
bool player_push_request(Player *p, const PlayerRequest *r)
{
    AcquireSRWLockExclusive(&p->lock);
    bool space = p->request_count < PLAYER_REQUEST_CAP;
    if (space) {
        p->requests[(p->request_head + p->request_count) % PLAYER_REQUEST_CAP] = *r;
        ++p->request_count; SetEvent(p->requests_ready);
    }
    ReleaseSRWLockExclusive(&p->lock); return space;
}
PlayerResult player_take_request(Player *p, PlayerRequest *r)
{
    if (!p || !r) { return PLAYER_INVALID; }
    AcquireSRWLockExclusive(&p->lock);
    PlayerResult result = p->destroyed ? PLAYER_CLOSED : PLAYER_MORE;
    if (!p->destroyed && p->request_count) {
        *r = p->requests[p->request_head]; p->request_head = (p->request_head + 1) % PLAYER_REQUEST_CAP;
        --p->request_count; if (!p->request_count) { ResetEvent(p->requests_ready); }
        SetEvent(p->wake); result = PLAYER_OK;
    }
    ReleaseSRWLockExclusive(&p->lock); return result;
}
HANDLE player_request_event(Player *p) { return p ? p->requests_ready : NULL; }
void player_publish(Player *p)
{
    unsigned index = p->marker_count ? p->markers[p->marker_head].index : p->desired_index;
    p->view.duration_frames = index < p->queue_count ? p->durations[index] : 0;
    AcquireSRWLockExclusive(&p->lock);
    bool changed = p->view.generation != p->published.generation || p->view.state != p->published.state ||
        p->view.position_frames != p->published.position_frames || p->view.duration_frames != p->published.duration_frames ||
        p->view.volume != p->published.volume || p->view.rate != p->published.rate || p->view.error != p->published.error ||
        p->view.timeline_valid != p->published.timeline_valid || p->queue_count != p->published_queue.count;
    p->published = p->view;
    p->published_queue.player = p->view; p->published_queue.count = p->queue_count; p->published_queue.index = 0;
    for (unsigned i = 0; i < p->queue_count; ++i) {
        changed |= memcmp(p->published_queue.ids[i], p->ids[p->order[i]], 16) != 0;
        memcpy(p->published_queue.ids[i], p->ids[p->order[i]], 16);
        if (p->order[i] == index) { p->published_queue.index = i; }
    }
    if (changed) { SetEvent(p->state_changed); }
    ReleaseSRWLockExclusive(&p->lock);
}
void player_queue_snapshot(Player *p, PlayerQueueSnapshot *out)
{
    if (!out) { return; } *out = (PlayerQueueSnapshot){0}; if (!p) { return; }
    AcquireSRWLockShared(&p->lock); *out = p->published_queue; ReleaseSRWLockShared(&p->lock);
}
HANDLE player_state_event(Player *p) { return p ? p->state_changed : NULL; }
void player_take_state(Player *p, PlayerQueueSnapshot *out)
{
    if (!out) { return; } *out = (PlayerQueueSnapshot){0}; if (!p) { return; }
    AcquireSRWLockExclusive(&p->lock); *out = p->published_queue; ResetEvent(p->state_changed); ReleaseSRWLockExclusive(&p->lock);
}
PlayerSnapshot player_snapshot(Player *p)
{
    PlayerSnapshot s = {0}; if (!p) { return s; }
    AcquireSRWLockShared(&p->lock); s = p->published; ReleaseSRWLockShared(&p->lock); return s;
}
bool player_next_index(Player *p, unsigned index, bool automatic, unsigned *next)
{
    if (!p->queue_count) { return false; }
    if (automatic && p->repeat == PLAYER_REPEAT_ONE) { *next = index; return true; }
    unsigned at = 0; while (at < p->queue_count && p->order[at] != index) { ++at; }
    if (++at >= p->queue_count) {
        if (p->repeat != PLAYER_REPEAT_ALL) { return false; } at = 0;
    }
    *next = p->order[at]; return true;
}
void player_queue_shuffle(Player *p, uint32_t seed)
{
    unsigned current = p->desired_index;
    if (p->marker_count) { current = p->markers[p->marker_head].index; }
    for (unsigned i = 0; i < p->queue_count; ++i) { p->order[i] = i; }
    p->shuffle = seed;
    if (!seed || p->queue_count < 2) { return; }
    p->order[current] = 0; p->order[0] = current;
    for (unsigned i = p->queue_count - 1; i > 1; --i) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        unsigned j = 1 + seed % i, t = p->order[i]; p->order[i] = p->order[j]; p->order[j] = t;
    }
}
