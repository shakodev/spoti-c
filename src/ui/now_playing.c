#include "app.h"
#include <stdio.h>
#include <string.h>

void app_now_playing(App *a)
{
    const PlayerSnapshot *p = &a->model.player;
    bool enabled = a->model.logged_in && p->state != PLAYER_STOPPED && p->state != PLAYER_ERROR && p->state != PLAYER_ENDED;
    uint64_t seconds = (p->timeline_valid || p->state == PLAYER_PAUSED) && p->rate ? p->position_frames / p->rate : 0;
    uint64_t duration = p->rate ? p->duration_frames / p->rate : 0;
    (void)snprintf(a->time,sizeof(a->time),"%llu:%02llu / %llu:%02llu",
        (unsigned long long)(seconds/60),(unsigned long long)(seconds%60),
        (unsigned long long)(duration/60),(unsigned long long)(duration%60));
    CLAY(CLAY_ID("AppNowPlaying"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(a->geometry.bar)},
        .padding={12,12,6,6},.childGap=4,.layoutDirection=CLAY_TOP_TO_BOTTOM},
        .backgroundColor={15,15,15,255},.cornerRadius=CLAY_CORNER_RADIUS(8),.clip={.horizontal=true,.vertical=true}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(18)},.childGap=12}}) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(18)}},.clip={.horizontal=true}}) {
                const char *title = a->model.playing.title[0] ? a->model.playing.title : "No track selected";
                app_label((Clay_String){false,(int32_t)strlen(title),title},16,(Clay_Color){240,240,240,255});
            }
            app_label((Clay_String){false,(int32_t)strlen(a->time),a->time},14,(Clay_Color){160,160,160,255});
        }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},.childGap=8,
            .childAlignment={.x=CLAY_ALIGN_X_CENTER}}}) {
            (void)app_button(a,CLAY_ID("PlayerPrevious"),CLAY_STRING("Prev"),52,(AppAction){.type=APP_PREVIOUS},!enabled);
            (void)app_button(a,CLAY_ID("PlayerPlay"),p->state == PLAYER_PLAYING ? CLAY_STRING("Pause") : CLAY_STRING("Play"),
                76,(AppAction){.type=APP_TOGGLE_PLAY},!enabled);
            (void)app_button(a,CLAY_ID("PlayerNext"),CLAY_STRING("Next"),52,(AppAction){.type=APP_NEXT},!enabled);
        }
        bool seek = enabled && p->duration_frames && (p->timeline_valid || p->state == PLAYER_PAUSED);
        if (seek) {
            if (a->control_count < APP_CONTROLS) {
                a->controls[a->control_count++] = (AppControl){.id=CLAY_ID("AppSeek"),.action={.type=APP_SEEK}};
            }
            ui_seek_slider(&a->widgets,CLAY_ID("AppSeek"),&a->widgets.seeks[0],a->geometry.width-40);
            double value;
            if (ui_seek_take_commit(&a->widgets.seeks[0],&value)) { (void)app_enqueue(a,(AppAction){.type=APP_SEEK,.value=value}); }
        } else {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(8)}},.backgroundColor={45,45,45,255}}) { }
        }
    }
}
