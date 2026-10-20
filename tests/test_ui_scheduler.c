#include "ui/ui_scheduler.h"
#include "support/test_check.h"

static void test_dirty_and_visibility(void)
{
    UiScheduler ui;
    ui_scheduler_init(&ui);
    CHECK(ui_scheduler_wait(&ui, 0) == UINT32_MAX);
    ui_scheduler_dirty(&ui, UI_DIRTY_INPUT);
    ui_scheduler_dirty(&ui, UI_DIRTY_STATE | UI_DIRTY_INPUT);
    CHECK(ui_scheduler_frame(&ui, 0) == 0);
    CHECK(ui_scheduler_wait(&ui, 0) == UINT32_MAX);
    ui_scheduler_visibility(&ui, true, false, 100);
    CHECK(ui_scheduler_wait(&ui, 100) == 0);
    CHECK(ui_scheduler_frame(&ui, 100) == (UI_DIRTY_INPUT | UI_DIRTY_STATE | UI_DIRTY_PAINT));
    CHECK(ui.stats.renders == 1);
    CHECK(ui_scheduler_frame(&ui, 100) == 0);
    CHECK(ui_scheduler_wait(&ui, 100) == UINT32_MAX);
    ui_scheduler_visibility(&ui, true, true, 200);
    ui_scheduler_dirty(&ui, UI_DIRTY_SIZE);
    CHECK(ui_scheduler_frame(&ui, 10000) == 0);
    CHECK(ui_scheduler_wait(&ui, 10000) == UINT32_MAX);
    ui_scheduler_visibility(&ui, true, false, 10000);
    CHECK(ui_scheduler_frame(&ui, 10000) == (UI_DIRTY_SIZE | UI_DIRTY_PAINT));
    CHECK(ui_scheduler_wait(&ui, 10000) == UINT32_MAX);
}

static void test_playback(void)
{
    UiScheduler ui; ui_scheduler_init(&ui);
    ui_scheduler_visibility(&ui, true, false, 0);
    ui_scheduler_playing(&ui, true, 0);
    CHECK(ui_scheduler_frame(&ui, 0) != 0);
    CHECK(ui_scheduler_deadline(&ui) == 1000);
    for (uint64_t now = 1; now < 1000; ++now) { CHECK(ui_scheduler_frame(&ui, now) == 0); }
    CHECK(ui_scheduler_frame(&ui, 1000) == UI_DIRTY_PROGRESS);
    CHECK(ui_scheduler_wait(&ui, 1000) == 1000);
    CHECK(ui_scheduler_frame(&ui, 15000) == UI_DIRTY_PROGRESS);
    CHECK(ui.stats.progress_ticks == 2);
    CHECK(ui_scheduler_frame(&ui, 15000) == 0);
    ui_scheduler_visibility(&ui, false, false, 15001);
    CHECK(ui_scheduler_frame(&ui, 90000) == 0);
    CHECK(ui_scheduler_wait(&ui, 90000) == UINT32_MAX);
    ui_scheduler_visibility(&ui, true, false, 90000);
    CHECK(ui_scheduler_frame(&ui, 90000) == UI_DIRTY_PAINT);
    CHECK(ui_scheduler_deadline(&ui) == 91000);
    ui_scheduler_playing(&ui, false, 90001);
    CHECK(ui_scheduler_frame(&ui, 90001) == UI_DIRTY_STATE);
    CHECK(ui_scheduler_wait(&ui, 90001) == UINT32_MAX);
    CHECK(ui_scheduler_frame(&ui, 100000) == 0);
}

static void test_animation(void)
{
    UiScheduler ui; ui_scheduler_init(&ui);
    ui_scheduler_visibility(&ui, true, false, 0);
    (void)ui_scheduler_frame(&ui, 0);
    CHECK(!ui_scheduler_animate(&ui, UI_ANIMATION_MAX_MS + 1, 0));
    CHECK(ui_scheduler_animate(&ui, 101, 0));
    CHECK(ui_scheduler_deadline(&ui) == 17);
    uint64_t last = 0, frames = 0;
    for (uint64_t now = 1; now <= 2000; ++now) {
        if (ui_scheduler_frame(&ui, now)) {
            CHECK(now - last >= 17); last = now; ++frames;
        }
    }
    CHECK(frames == 6 && last == 102);
    CHECK(ui_scheduler_wait(&ui, 2000) == UINT32_MAX);
    CHECK(ui_scheduler_animate(&ui, 100, 2000));
    CHECK(ui_scheduler_animate(&ui, 100, 2001));
    CHECK(ui_scheduler_deadline(&ui) == 2017);
    CHECK(ui_scheduler_animate(&ui, 0, 2002));
    CHECK(ui_scheduler_wait(&ui, 2002) == UINT32_MAX);
    CHECK(ui_scheduler_animate(&ui, 100, 3000));
    CHECK(ui_scheduler_frame(&ui, 9000) == UI_DIRTY_ANIMATION);
    CHECK(ui_scheduler_frame(&ui, 9000) == 0);
    CHECK(ui_scheduler_animate(&ui, 100, 10000));
    ui_scheduler_visibility(&ui, true, true, 10001);
    CHECK(ui_scheduler_wait(&ui, 11000) == UINT32_MAX);
    ui_scheduler_visibility(&ui, true, false, 12000);
    (void)ui_scheduler_frame(&ui, 12000);
    CHECK(ui_scheduler_wait(&ui, 12000) == UINT32_MAX);
}

static void test_lyrics(void)
{
    UiScheduler ui; ui_scheduler_init(&ui);
    ui_scheduler_visibility(&ui, true, false, 0);
    ui_scheduler_playing(&ui, true, 0);
    ui_scheduler_lyrics(&ui, true, 250);
    (void)ui_scheduler_frame(&ui, 0);
    CHECK(ui_scheduler_deadline(&ui) == 250);
    CHECK(ui_scheduler_frame(&ui, 249) == 0);
    CHECK(ui_scheduler_frame(&ui, 250) == UI_DIRTY_LYRICS);
    CHECK(ui_scheduler_deadline(&ui) == 1000);
    ui_scheduler_lyrics(&ui, true, 250);
    CHECK(ui_scheduler_frame(&ui, 251) == 0);
    ui_scheduler_lyrics(&ui, true, 300);
    ui_scheduler_lyrics(&ui, false, 300);
    CHECK(ui_scheduler_frame(&ui, 300) == 0);
    ui_scheduler_lyrics(&ui, true, 500);
    ui_scheduler_playing(&ui, false, 350);
    (void)ui_scheduler_frame(&ui, 350);
    CHECK(ui_scheduler_wait(&ui, 1000) == UINT32_MAX);
    ui_scheduler_playing(&ui, true, 2000);
    ui_scheduler_lyrics(&ui, true, 2500);
    (void)ui_scheduler_frame(&ui, 2000);
    CHECK(ui_scheduler_frame(&ui, 2500) == UI_DIRTY_LYRICS);
    CHECK(ui.stats.lyrics_ticks == 2);
}

static void test_reduced_motion(void)
{
    UiScheduler ui; ui_scheduler_init(&ui);
    ui_scheduler_visibility(&ui,true,false,0); (void)ui_scheduler_frame(&ui,0);
    CHECK(!ui.reduced_motion && ui_scheduler_animate(&ui,100,0));
    ui_scheduler_reduce_motion(&ui,true);
    CHECK(ui.reduced_motion && ui.animation == CORE_CLOCK_NEVER && ui.animation_end == CORE_CLOCK_NEVER);
    CHECK(ui_scheduler_frame(&ui,1) == (UI_DIRTY_STATE | UI_DIRTY_ANIMATION));
    CHECK(ui_scheduler_wait(&ui,1) == UINT32_MAX && ui.stats.animation_ticks == 0);
    ui_scheduler_reduce_motion(&ui,true);
    CHECK(ui_scheduler_frame(&ui,2) == 0); /* Repeated settings broadcasts stay idle. */
    CHECK(ui_scheduler_animate(&ui,100,2));
    CHECK(ui_scheduler_frame(&ui,2) == UI_DIRTY_ANIMATION); /* Settle once immediately. */
    for (uint64_t now = 3; now < 1000; ++now) {
        CHECK(ui_scheduler_frame(&ui,now) == 0 && ui_scheduler_wait(&ui,now) == UINT32_MAX);
    }
    ui_scheduler_playing(&ui,true,1000); (void)ui_scheduler_frame(&ui,1000);
    CHECK(ui_scheduler_deadline(&ui) == 2000);
    CHECK(ui_scheduler_frame(&ui,2000) == UI_DIRTY_PROGRESS); /* Playback is unchanged. */
    ui_scheduler_playing(&ui,false,2001); (void)ui_scheduler_frame(&ui,2001);
    ui_scheduler_visibility(&ui,false,false,2100);
    CHECK(ui_scheduler_animate(&ui,100,2100));
    CHECK(ui_scheduler_wait(&ui,2200) == UINT32_MAX && ui_scheduler_frame(&ui,2200) == 0);
    ui_scheduler_visibility(&ui,true,false,2300);
    CHECK(ui_scheduler_frame(&ui,2300) == (UI_DIRTY_PAINT | UI_DIRTY_ANIMATION));
    CHECK(ui_scheduler_wait(&ui,2300) == UINT32_MAX);
    ui_scheduler_reduce_motion(&ui,false);
    CHECK(ui_scheduler_frame(&ui,2400) == UI_DIRTY_STATE);
    CHECK(ui_scheduler_wait(&ui,2400) == UINT32_MAX); /* Never restart a retired transition. */
    CHECK(ui_scheduler_animate(&ui,100,2400) && ui_scheduler_deadline(&ui) == 2417);
    CHECK(ui_scheduler_frame(&ui,2417) == UI_DIRTY_ANIMATION && ui.stats.animation_ticks == 1);
}

int main(void)
{
    test_dirty_and_visibility(); test_playback(); test_animation(); test_lyrics(); test_reduced_motion();
    puts("ui_scheduler: fake-clock coalescing, playback, visibility, animation and lyrics passed");
    return 0;
}
