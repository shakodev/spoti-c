#include "ui_scheduler.h"
#include <string.h>

static bool drawable(const UiScheduler *ui) { return ui->visible && !ui->occluded; }

void ui_scheduler_init(UiScheduler *ui)
{
    memset(ui, 0, sizeof(*ui));
    ui->progress = ui->animation = ui->animation_end = ui->lyrics = CORE_CLOCK_NEVER;
    ui->lyrics_consumed = CORE_CLOCK_NEVER;
    ui->work = CORE_CLOCK_NEVER;
}

void ui_scheduler_dirty(UiScheduler *ui, uint32_t reasons) { ui->dirty |= reasons; }

void ui_scheduler_visibility(UiScheduler *ui, bool visible, bool occluded, uint64_t now)
{
    bool before = drawable(ui);
    ui->visible = visible; ui->occluded = occluded;
    if (before == drawable(ui)) { return; }
    ui->animation = ui->animation_end = CORE_CLOCK_NEVER;
    ui->progress = drawable(ui) && ui->playing ? core_deadline_after(now, 1000) : CORE_CLOCK_NEVER;
    if (drawable(ui)) { ui->dirty |= UI_DIRTY_PAINT; }
}

void ui_scheduler_playing(UiScheduler *ui, bool playing, uint64_t now)
{
    if (ui->playing == playing) { return; }
    ui->playing = playing;
    ui->progress = playing && drawable(ui) ? core_deadline_after(now, 1000) : CORE_CLOCK_NEVER;
    if (!playing) { ui->lyrics = CORE_CLOCK_NEVER; }
    ui->dirty |= UI_DIRTY_STATE;
}

void ui_scheduler_lyrics(UiScheduler *ui, bool visible, uint64_t next_boundary)
{
    ui->lyrics_visible = visible;
    ui->lyrics = visible && ui->playing && next_boundary != ui->lyrics_consumed ?
        next_boundary : CORE_CLOCK_NEVER;
}

void ui_scheduler_reduce_motion(UiScheduler *ui, bool reduced)
{
    if (ui->reduced_motion == reduced) { return; }
    ui->reduced_motion = reduced; ui->dirty |= UI_DIRTY_STATE;
    if (reduced) {
        if (ui->animation != CORE_CLOCK_NEVER) { ui->dirty |= UI_DIRTY_ANIMATION; }
        ui->animation = ui->animation_end = CORE_CLOCK_NEVER;
    }
}

bool ui_scheduler_animate(UiScheduler *ui, uint32_t duration_ms, uint64_t now)
{
    if (duration_ms > UI_ANIMATION_MAX_MS) { return false; }
    if (!duration_ms || !drawable(ui) || ui->reduced_motion) {
        ui->animation = ui->animation_end = CORE_CLOCK_NEVER;
        if (duration_ms && ui->reduced_motion) { ui->dirty |= UI_DIRTY_ANIMATION; }
    } else {
        ui->animation_end = core_deadline_after(now, duration_ms);
        uint64_t next = core_deadline_after(now, UI_ANIMATION_INTERVAL_MS);
        /* Repeated input may extend a transition, but cannot accelerate frames. */
        if (ui->animation == CORE_CLOCK_NEVER) { ui->animation = next; }
    }
    return true;
}
void ui_scheduler_work(UiScheduler *ui, uint64_t deadline) { ui->work = deadline; }

uint64_t ui_scheduler_deadline(const UiScheduler *ui)
{
    if (!drawable(ui)) { return CORE_CLOCK_NEVER; }
    if (ui->dirty) { return 0; }
    uint64_t next = ui->progress;
    if (ui->animation < next) { next = ui->animation; }
    if (ui->work < next) { next = ui->work; }
    if (ui->playing && ui->lyrics_visible && ui->lyrics < next) { next = ui->lyrics; }
    return next;
}

uint32_t ui_scheduler_wait(const UiScheduler *ui, uint64_t now)
{
    return core_deadline_wait(now, ui_scheduler_deadline(ui));
}

uint32_t ui_scheduler_frame(UiScheduler *ui, uint64_t now)
{
    if (!drawable(ui)) { return 0; }
    if (core_deadline_expired(now,ui->work)) { ui->dirty |= UI_DIRTY_STATE; ui->work = CORE_CLOCK_NEVER; }
    if (core_deadline_expired(now, ui->progress)) {
        ui->dirty |= UI_DIRTY_PROGRESS; ++ui->stats.progress_ticks;
        ui->progress = core_deadline_after(now, 1000);
    }
    if (core_deadline_expired(now, ui->animation)) {
        ui->dirty |= UI_DIRTY_ANIMATION; ++ui->stats.animation_ticks;
        if (now >= ui->animation_end) { ui->animation = ui->animation_end = CORE_CLOCK_NEVER; }
        else {
            ui->animation = core_deadline_after(now, UI_ANIMATION_INTERVAL_MS);
        }
    }
    if (ui->playing && ui->lyrics_visible && core_deadline_expired(now, ui->lyrics)) {
        ui->dirty |= UI_DIRTY_LYRICS; ++ui->stats.lyrics_ticks;
        ui->lyrics_consumed = ui->lyrics;
        ui->lyrics = CORE_CLOCK_NEVER;
    }
    uint32_t reasons = ui->dirty;
    ui->dirty = 0;
    if (reasons) { ++ui->stats.renders; }
    return reasons;
}
