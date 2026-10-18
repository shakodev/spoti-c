#include "player_internal.h"

void player_timeline(Player *p)
{
    if (!p->marker_count || p->view.state == PLAYER_PAUSED || p->view.state == PLAYER_STOPPED ||
        p->view.state == PLAYER_ERROR || p->view.state == PLAYER_ENDED) { return; }
    if (atomic_load(&p->config.sink->invalidated)) { p->view.timeline_valid = false; return; }
    SinkInfo i = sink_info(p->config.sink);
    if (i.timeline_overflow) { player_fail(p, AUDIO_LIMIT); return; }
    p->view.timeline_valid = i.timeline_valid;
    if (!i.timeline_valid) { return; }
    while (p->marker_count > 1) {
        PlayerMarker *next = &p->markers[(p->marker_head + 1) % PLAYER_MARKERS];
        if (next->media > i.played_media_frames) { break; }
        p->marker_head = (p->marker_head + 1) % PLAYER_MARKERS; --p->marker_count;
    }
    PlayerMarker *m = &p->markers[p->marker_head];
    uint64_t elapsed = i.played_media_frames >= m->media ? i.played_media_frames - m->media : 0;
    if (m->position > UINT64_MAX - elapsed) { player_fail(p, AUDIO_LIMIT); return; }
    p->view.position_frames = m->position + elapsed;
    p->view.generation = m->generation; memcpy(p->view.track_id, p->ids[m->index], 16);
    if (p->final_eof && i.played_media_frames >= p->written) {
        if (p->pending_error != AUDIO_OK) { player_fail(p, p->pending_error); return; }
        p->view.state = PLAYER_ENDED; p->want_play = false;
        (void)sink_stop(p->config.sink); player_retire_all(p);
    }
}
