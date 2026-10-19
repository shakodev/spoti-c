#include "lyrics.h"
#include <string.h>

uint32_t lyrics_line_at(const LyricsDocument *doc, uint64_t position_ms)
{
    if (doc == NULL || doc->availability != LYRICS_AVAILABLE || doc->sync == LYRICS_PLAIN || doc->count == 0) { return LYRICS_NO_LINE; }
    uint32_t low = 0, high = doc->count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        if (doc->lines[middle].start_ms <= position_ms) { low = middle + 1; }
        else { high = middle; }
    }
    return low == 0 ? LYRICS_NO_LINE : low - 1;
}

static bool frame_ms(uint64_t frames, uint32_t rate, bool ceiling, uint64_t *ms)
{
    if (rate == 0 || frames / rate > UINT64_MAX / 1000) { return false; }
    uint64_t whole = frames / rate * 1000, remainder = frames % rate * 1000;
    uint64_t part = remainder / rate + (ceiling && remainder % rate != 0);
    if (part > UINT64_MAX - whole) { return false; }
    *ms = whole + part; return true;
}
static bool boundary_frame(uint64_t ms, uint32_t rate, uint64_t *frames)
{
    if (rate == 0 || ms / 1000 > UINT64_MAX / rate) { return false; }
    uint64_t whole = ms / 1000 * rate, remainder = ms % 1000 * rate;
    uint64_t part = remainder / 1000 + (remainder % 1000 != 0);
    if (part > UINT64_MAX - whole) { return false; }
    *frames = whole + part; return true;
}
static bool matches(const LyricsDocument *doc, const PlayerSnapshot *audible)
{
    return doc != NULL && audible != NULL && audible->rate != 0 && memcmp(doc->track_id, audible->track_id, 16) == 0;
}

uint64_t lyrics_next_deadline(const LyricsDocument *doc, const PlayerSnapshot *audible, bool visible, uint64_t now_ms)
{
    if (!visible || !matches(doc, audible) || !audible->timeline_valid || audible->state != PLAYER_PLAYING ||
        doc->availability != LYRICS_AVAILABLE || doc->sync == LYRICS_PLAIN || doc->count == 0) { return CORE_CLOCK_NEVER; }
    uint64_t position;
    if (!frame_ms(audible->position_frames, audible->rate, false, &position)) { return CORE_CLOCK_NEVER; }
    uint32_t current = lyrics_line_at(doc, position), next = current == LYRICS_NO_LINE ? 0 : current + 1;
    if (next >= doc->count) { return CORE_CLOCK_NEVER; }
    uint64_t boundary, delay;
    if (!boundary_frame(doc->lines[next].start_ms, audible->rate, &boundary) || boundary <= audible->position_frames ||
        !frame_ms(boundary - audible->position_frames, audible->rate, true, &delay) || delay >= UINT64_MAX - now_ms) {
        return CORE_CLOCK_NEVER;
    }
    return now_ms + delay;
}

bool lyrics_timeline_set(LyricsTimeline *timeline, const LyricsDocument *doc)
{
    if (timeline == NULL) { return false; }
    if (doc != NULL && !lyrics_document_retain(doc)) { return false; }
    const LyricsDocument *old = timeline->document; timeline->document = doc;
    if (old != NULL) { lyrics_document_release(old); }
    return true;
}

LyricsView lyrics_timeline_update(const LyricsTimeline *timeline, const PlayerSnapshot *audible, bool visible, uint64_t now_ms)
{
    LyricsView view = {.line = LYRICS_NO_LINE, .deadline_ms = CORE_CLOCK_NEVER};
    const LyricsDocument *doc = timeline == NULL ? NULL : timeline->document;
    if (!matches(doc, audible) || (audible->state != PLAYER_PLAYING && audible->state != PLAYER_BUFFERING &&
        audible->state != PLAYER_PAUSED && audible->state != PLAYER_ENDED) ||
        (!audible->timeline_valid && audible->state != PLAYER_PAUSED && audible->state != PLAYER_ENDED) ||
        !frame_ms(audible->position_frames, audible->rate, false, &view.position_ms)) { return view; }
    view.valid = true; view.line = lyrics_line_at(doc, view.position_ms);
    view.deadline_ms = lyrics_next_deadline(doc, audible, visible, now_ms); return view;
}
