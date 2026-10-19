#include "player_internal.h"
#include "core/thread.h"

PlayerResult player_create(Arena *core, const PlayerConfig *config, Player **out)
{
    if (!out) { return PLAYER_INVALID; } *out = NULL;
    if (!core || core->capacity > MEMORY_BUDGET_CORE || !config || !config->sink ||
        !config->sink->opened || config->sink->owner != GetCurrentThreadId() || core_thread_is_gui() ||
        atomic_load(&config->sink->state) != SINK_STOPPED) { return PLAYER_INVALID; }
    Arena *arenas[4] = {config->codec[0], config->codec[1], config->work[0], config->work[1]};
    for (unsigned i = 0; i < 4; ++i) {
        if (!arenas[i] || arenas[i]->used || arenas[i]->capacity != (i < 2 ? DECODER_CODEC_BYTES : DECODER_WORK_BYTES)) {
            return PLAYER_INVALID;
        }
        uintptr_t a = (uintptr_t)arenas[i]->base, cb = (uintptr_t)core->base, pcm = (uintptr_t)config->sink->pcm;
        if ((a < cb + core->capacity && cb < a + arenas[i]->capacity) ||
            (a < pcm + config->sink->pcm_bytes && pcm < a + arenas[i]->capacity)) { return PLAYER_INVALID; }
        for (unsigned j = 0; j < i; ++j) {
            uintptr_t b = (uintptr_t)arenas[j]->base;
            if (a < b + arenas[j]->capacity && b < a + arenas[i]->capacity) { return PLAYER_INVALID; }
        }
    }
    ArenaMark mark = arena_mark(core);
    Player *p = arena_alloc(core, sizeof(*p), _Alignof(Player)); if (!p) { return PLAYER_NO_MEMORY; }
    memset(p, 0, sizeof(*p)); p->config = *config; p->owner = GetCurrentThreadId();
    InitializeSRWLock(&p->lock);
    p->wake = CreateEventW(NULL, TRUE, FALSE, NULL);
    p->requests_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    p->state_changed = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!p->wake || !p->requests_ready || !p->state_changed) {
        if (p->wake) { CloseHandle(p->wake); } if (p->requests_ready) { CloseHandle(p->requests_ready); }
        if (p->state_changed) { CloseHandle(p->state_changed); }
        (void)arena_rewind(core, mark); return PLAYER_NO_MEMORY;
    }
    p->current = p->next = -1; p->view.volume = 1;
    p->view.rate = config->sink->format.sample_rate; p->view.state = PLAYER_STOPPED;
    player_publish(p); *out = p; return PLAYER_OK;
}
void player_fail(Player *p, AudioResult error)
{
    (void)sink_stop(p->config.sink); player_retire_all(p);
    p->desired = p->final_eof = false; p->view.state = PLAYER_ERROR; p->view.error = error;
}
void player_begin(Player *p, unsigned index, uint64_t position, PlayerState state)
{
    if (p->epoch == UINT64_MAX) { player_fail(p, AUDIO_LIMIT); return; }
    (void)sink_stop(p->config.sink); player_retire_all(p);
    p->written = 0; p->marker_count = p->marker_head = 0; p->final_eof = false;
    p->pending_error = AUDIO_OK;
    p->desired = true; p->desired_index = index; p->desired_position = position;
    p->view.generation = ++p->epoch; memcpy(p->view.track_id, p->ids[index], 16);
    p->view.position_frames = position; p->view.rate = p->config.sink->format.sample_rate;
    p->view.state = state; p->view.error = AUDIO_OK; p->view.timeline_valid = false;
    if (sink_prepare(p->config.sink) != AUDIO_OK) { player_fail(p, AUDIO_BAD_STATE); }
}
static void device_change(Player *p)
{
    uint32_t old_rate = p->view.rate;
    uint64_t position = p->view.position_frames;
    unsigned index = p->marker_count ? p->markers[p->marker_head].index : p->desired_index;
    bool active = p->current >= 0 || p->desired || p->view.state == PLAYER_PAUSED;
    (void)sink_stop(p->config.sink); player_retire_all(p);
    AudioResult r = sink_switch(p->config.sink);
    if (r != AUDIO_OK) { player_fail(p, r); return; }
    p->view.rate = p->config.sink->format.sample_rate;
    memset(p->durations, 0, sizeof(p->durations));
    if (p->epoch == UINT64_MAX) { player_fail(p, AUDIO_LIMIT); return; }
    p->view.generation = ++p->epoch;
    (void)sink_set_volume(p->config.sink, p->view.volume);
    if (active && p->queue_count && old_rate && position <= UINT64_MAX / p->view.rate) {
        player_begin(p, index, position * p->view.rate / old_rate, PLAYER_SEEKING);
    } else if (active) { player_fail(p, AUDIO_LIMIT); }
}
PlayerResult player_pump(Player *p)
{
    if (!player_owner(p) || p->busy) { return PLAYER_BAD_STATE; }
    p->busy = true; p->more = false;
    AcquireSRWLockExclusive(&p->lock); ResetEvent(p->wake);
    p->view.clock_serial=p->clock_requested; ReleaseSRWLockExclusive(&p->lock);
    player_timeline(p);
    if (atomic_load(&p->config.sink->invalidated)) { device_change(p); }
    PlayerCommand command;
    for (unsigned n = 0; n < PLAYER_COMMAND_CAP && player_pop_command(p, &command); ++n) { player_command(p, &command); }
    player_sources_drive(p); player_decode_drive(p); player_sources_drive(p); player_timeline(p);
    player_publish(p);
    AcquireSRWLockShared(&p->lock);
    bool queued = p->command_count != 0 || p->clock_requested!=p->view.clock_serial, requests = p->request_count != 0;
    ReleaseSRWLockShared(&p->lock);
    bool closed = p->closing && !queued && !requests && p->slots[0].state == PS_FREE && p->slots[1].state == PS_FREE;
    p->more |= queued; p->busy = false;
    return closed ? PLAYER_CLOSED : p->more ? PLAYER_MORE : PLAYER_OK;
}
size_t player_wait_handles(Player *p, HANDLE output[3])
{
    if (!player_owner(p) || !output) { return 0; }
    size_t count = 0; output[count++] = p->wake;
    if (p->config.sink->control_event) { output[count++] = p->config.sink->control_event; }
    if (atomic_load(&p->config.sink->state) == SINK_RUNNING && p->config.sink->producer_event) {
        output[count++] = p->config.sink->producer_event;
    }
    return count;
}
DWORD player_next_wait_ms(Player *p)
{
    if (!player_owner(p)) { return INFINITE; }
    if (p->more) { return 0; }
    if (!p->want_play || (p->view.state != PLAYER_PLAYING && p->view.state != PLAYER_BUFFERING)) { return INFINITE; }
    uint64_t frames = UINT64_MAX;
    if (p->marker_count > 1) {
        PlayerMarker *a = &p->markers[p->marker_head], *b = &p->markers[(p->marker_head + 1) % PLAYER_MARKERS];
        uint64_t at = a->media + (p->view.position_frames - a->position);
        if (b->media > at) { frames = b->media - at; }
    } else if (p->final_eof && p->marker_count) {
        PlayerMarker *a = &p->markers[p->marker_head];
        uint64_t at = a->media + (p->view.position_frames - a->position);
        if (p->written > at) { frames = p->written - at; }
    }
    if (frames < p->view.rate && p->view.rate) { return (DWORD)(frames * 1000 / p->view.rate) + 1; }
    return 1000;
}
PlayerResult player_destroy(Player *p)
{
    if (!player_owner(p) || p->busy || !p->closing) { return PLAYER_BAD_STATE; }
    AcquireSRWLockExclusive(&p->lock);
    if (p->command_count || p->request_count || p->slots[0].state != PS_FREE || p->slots[1].state != PS_FREE) {
        ReleaseSRWLockExclusive(&p->lock); return PLAYER_BAD_STATE;
    }
    p->destroyed = true; CloseHandle(p->wake); CloseHandle(p->requests_ready); CloseHandle(p->state_changed);
    p->wake = p->requests_ready = p->state_changed = NULL; ReleaseSRWLockExclusive(&p->lock); return PLAYER_OK;
}
