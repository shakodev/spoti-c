#include "player_internal.h"

void player_retire_slot(Player *p, unsigned at)
{
    PlayerSlot *s = &p->slots[at];
    if (s->state == PS_FREE || s->state == PS_RETIRING) { return; }
    if (s->decoder) { (void)decoder_close(s->decoder); s->decoder = NULL; }
    s->frames = s->offset = 0; memset(s->block, 0, sizeof(s->block));
    s->state = PS_RETIRING; s->retirement_sent = false; player_ticket_publish(p, at);
}
void player_retire_all(Player *p)
{
    player_retire_slot(p, 0); player_retire_slot(p, 1); p->current = p->next = -1;
}
static bool request_slot(Player *p, unsigned index, uint64_t target, bool prefetch, int *out)
{
    unsigned at = p->slots[0].state == PS_FREE ? 0 : 1;
    if (p->slots[at].state != PS_FREE) { return false; }
    if (p->serial > UINT64_MAX - 2) { player_fail(p, AUDIO_LIMIT); return false; }
    p->serial += 2;
    PlayerSlot *s = &p->slots[at];
    *s = (PlayerSlot){.state = PS_REQUESTED, .ticket = p->serial,
        .decoder_generation = p->serial, .target = target, .index = index, .targeted = target == 0};
    player_ticket_publish(p, at);
    PlayerRequest r = {.type = PLAYER_RESOLVE, .ticket = s->ticket, .generation = p->view.generation,
        .position_frames = target, .format = p->config.sink->format, .prefetch = prefetch};
    memcpy(r.track_id, p->ids[index], 16);
    if (!player_push_request(p, &r)) {
        *s = (PlayerSlot){0}; player_ticket_publish(p, at); return false;
    }
    *out = (int)at; return true;
}
void player_sources_drive(Player *p)
{
    for (unsigned i = 0; i < 2; ++i) {
        PlayerSlot *s = &p->slots[i];
        if (s->state == PS_RETIRING && !s->retirement_sent) {
            PlayerRequest r = {.type = PLAYER_RETIRE, .ticket = s->ticket,
                .generation = p->view.generation, .format = p->config.sink->format};
            memcpy(r.track_id, p->ids[s->index], 16);
            /* ID is informational on retirement; ticket is the authority. */
            if (player_push_request(p, &r)) { s->retirement_sent = true; }
        }
    }
    if (p->closing || p->view.state == PLAYER_ERROR) { return; }
    if (p->desired && p->current < 0 && request_slot(p, p->desired_index, p->desired_position, false, &p->current)) {
        p->desired = false; p->producer_index = p->desired_index;
    }
    unsigned next;
    if (p->current >= 0 && p->next < 0 && player_next_index(p, p->producer_index, true, &next)) {
        (void)request_slot(p, next, 0, true, &p->next);
    }
}
static AudioResult source_read(void *ctx, uint64_t generation, uint64_t offset, void *out, size_t cap, size_t *got)
{
    PlayerSlot *s = ctx; (void)generation;
    return s->source.read_at(s->source.context, s->ticket, offset, out, cap, got);
}
AudioResult player_slot_read(Player *p, unsigned at)
{
    PlayerSlot *s = &p->slots[at];
    if (s->state == PS_FAILED) { return s->failure; }
    if (s->state != PS_READY) { return AUDIO_NEED_MORE; }
    if (s->frames > s->offset) { return AUDIO_OK; }
    if (s->eof) { return AUDIO_EOF; }
    if (!s->decoder) {
        DecoderConfig dc = {.output = p->config.sink->format, .generation = s->decoder_generation,
            .source_bytes = s->source.bytes, .read_at = source_read, .context = s};
        AudioResult r = decoder_open(p->config.codec[at], p->config.work[at], &dc, &s->decoder);
        if (r != AUDIO_OK) { return r; }
    }
    if (!s->targeted) {
        if (!decoder_info(s->decoder).ready) {
            size_t discarded = 0;
            AudioResult r = decoder_read_frames(s->decoder, s->decoder_generation, s->block, PLAYER_BLOCK_FRAMES, &discarded);
            if (r != AUDIO_OK && r != AUDIO_NEED_MORE && r != AUDIO_EOF) { return r; }
            if (!decoder_info(s->decoder).ready) { return AUDIO_NEED_MORE; }
        }
        ++s->decoder_generation;
        AudioResult r = decoder_seek(s->decoder, s->decoder_generation, s->target);
        if (r != AUDIO_OK) { return r; } s->targeted = true;
    }
    s->frames = s->offset = 0;
    AudioResult r = decoder_read_frames(s->decoder, s->decoder_generation, s->block, PLAYER_BLOCK_FRAMES, &s->frames);
    if (r == AUDIO_EOF) { s->eof = true; }
    return r;
}
bool player_marker_add(Player *p, unsigned index, uint64_t position)
{
    if (p->marker_count == PLAYER_MARKERS || p->epoch == UINT64_MAX) { return false; }
    PlayerMarker m = {.media = p->written, .position = position, .generation = ++p->epoch, .index = index};
    p->markers[(p->marker_head + p->marker_count) % PLAYER_MARKERS] = m; ++p->marker_count;
    return true;
}
void player_decode_drive(Player *p)
{
    if (p->current < 0 || p->closing || p->view.state == PLAYER_ERROR) { return; }
    if (p->next >= 0) {
        AudioResult r = player_slot_read(p, (unsigned)p->next);
        if (r != AUDIO_OK && r != AUDIO_NEED_MORE && r != AUDIO_EOF) {
            p->slots[p->next].state = PS_FAILED; p->slots[p->next].failure = r;
        }
    }
    AudioSink *sink = p->config.sink;
    if (p->want_play && atomic_load(&sink->state) == SINK_STOPPED && p->written) {
        AudioResult r = sink_start(sink); if (r != AUDIO_OK) { player_fail(p, r); return; }
    }
    for (unsigned budget = 0; budget < 32; ++budget) {
        PlayerSlot *s = &p->slots[p->current];
        if (s->frames == s->offset && spsc_writable(&sink->ring) == 0) { break; }
        AudioResult r = player_slot_read(p, (unsigned)p->current);
        if (r == AUDIO_NEED_MORE) {
            if (p->want_play && spsc_writable(&sink->ring) == sink->ring.capacity) { p->view.state = PLAYER_BUFFERING; }
            break;
        }
        if (r == AUDIO_EOF) {
            if (!p->marker_count && !player_marker_add(p, s->index, s->target)) { player_fail(p, AUDIO_LIMIT); break; }
            unsigned index;
            if (!player_next_index(p, p->producer_index, true, &index)) {
                p->final_eof = true;
                if (!p->written) { p->view.state = p->want_play ? PLAYER_BUFFERING : PLAYER_PAUSED; }
                break;
            }
            if (p->next < 0 || p->marker_count == PLAYER_MARKERS) {
                if (p->want_play && spsc_writable(&sink->ring) == sink->ring.capacity) { p->view.state = PLAYER_BUFFERING; }
                break;
            }
            PlayerSlot *next = &p->slots[p->next];
            if (next->state == PS_FAILED) { p->pending_error = next->failure; p->final_eof = true; break; }
            r = player_slot_read(p, (unsigned)p->next);
            if (r == AUDIO_NEED_MORE) {
                if (p->want_play && spsc_writable(&sink->ring) == sink->ring.capacity) { p->view.state = PLAYER_BUFFERING; }
                break;
            }
            if (r != AUDIO_OK && r != AUDIO_EOF) { player_fail(p, r); break; }
            if (!player_marker_add(p, next->index, 0)) { player_fail(p, AUDIO_LIMIT); break; }
            player_retire_slot(p, (unsigned)p->current);
            p->current = p->next; p->next = -1; p->producer_index = next->index;
            player_sources_drive(p); continue;
        }
        if (r != AUDIO_OK) { player_fail(p, r); break; }
        if (!p->marker_count && !player_marker_add(p, s->index, s->target)) { player_fail(p, AUDIO_LIMIT); break; }
        if (s->frames == 0) { player_fail(p, AUDIO_CORRUPT); break; }
        if (p->written > UINT64_MAX - (s->frames - s->offset)) { player_fail(p, AUDIO_LIMIT); break; }
        size_t written = 0;
        r = sink_write(sink, s->block + s->offset * sink->format.channels, s->frames - s->offset, &written);
        s->offset += written; p->written += written;
        if (r != AUDIO_OK && r != AUDIO_FULL) { player_fail(p, r); break; }
        if (p->want_play && atomic_load(&sink->state) == SINK_STOPPED && p->written) {
            r = sink_start(sink); if (r != AUDIO_OK) { player_fail(p, r); break; }
        }
        p->view.state = p->want_play ? PLAYER_PLAYING : PLAYER_PAUSED;
        if (r == AUDIO_FULL) { break; }
        if (budget == 31) { p->more = true; }
    }
    (void)sink_producer_arm(sink);
}
