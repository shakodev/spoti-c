#ifndef SPOTIC_UI_SCHEDULER_H
#define SPOTIC_UI_SCHEDULER_H

#include "core/clock.h"

enum UiDirty {
    UI_DIRTY_INPUT = 1u, UI_DIRTY_STATE = 2u, UI_DIRTY_SIZE = 4u,
    UI_DIRTY_PAINT = 8u, UI_DIRTY_PROGRESS = 16u, UI_DIRTY_ANIMATION = 32u,
    UI_DIRTY_LYRICS = 64u, UI_DIRTY_DEVICE = 128u
};
#define UI_ANIMATION_INTERVAL_MS 17u /* Never more than 60 frames/second. */
#define UI_ANIMATION_MAX_MS 1000u

typedef struct UiSchedulerStats {
    uint64_t renders, progress_ticks, animation_ticks, lyrics_ticks;
} UiSchedulerStats;

/* UI-thread only, caller-owned. All times use the same monotonic ms domain.
 * Zero allocations. Hidden dirty state is retained until visibility returns. */
typedef struct UiScheduler {
    uint32_t dirty;
    bool visible, occluded, playing, lyrics_visible;
    uint64_t progress, animation, animation_end, lyrics, lyrics_consumed, work;
    UiSchedulerStats stats;
} UiScheduler;

void ui_scheduler_init(UiScheduler *ui);
void ui_scheduler_dirty(UiScheduler *ui, uint32_t reasons);
void ui_scheduler_visibility(UiScheduler *ui, bool visible, bool occluded, uint64_t now);
void ui_scheduler_playing(UiScheduler *ui, bool playing, uint64_t now);
/* Call when seeking/changing lyrics or after a consumed boundary.
 * CORE_CLOCK_NEVER disarms the boundary.
 * Identical consumed boundaries stay consumed; caller must advance the boundary. */
void ui_scheduler_lyrics(UiScheduler *ui, bool visible, uint64_t next_boundary);
/* One finite UI preparation deadline (search debounce / manual scroll hold). */
void ui_scheduler_work(UiScheduler *ui, uint64_t deadline);
/* Explicit finite transition. Zero stops; durations above the cap are rejected. */
bool ui_scheduler_animate(UiScheduler *ui, uint32_t duration_ms, uint64_t now);
uint64_t ui_scheduler_deadline(const UiScheduler *ui);
uint32_t ui_scheduler_wait(const UiScheduler *ui, uint64_t now);
uint32_t ui_scheduler_frame(UiScheduler *ui, uint64_t now);

#endif
