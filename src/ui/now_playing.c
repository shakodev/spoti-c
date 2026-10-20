#include "app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *unavailable(const App *a)
{
    if (!a->model.logged_in) { return "Sign in to control playback"; }
    if (!a->services.player && !a->services.submit) { return "Playback service unavailable"; }
    return a->model.player.state==PLAYER_STOPPED ? "Select a track to start playback" :
        a->model.player.state==PLAYER_ERROR ? "Playback failed. Select a track to retry" : NULL;
}
static void metadata(App *a)
{
    const PlayerSnapshot *p=&a->model.player;
    bool current=a->model.logged_in && !memcmp(p->track_id,a->model.playing.id,16) && a->model.playing.title[0];
    const char *title=current ? a->model.playing.title : p->state==PLAYER_STOPPED || !a->model.logged_in ? "No track selected" : "Loading track...";
    const char *artist=current ? a->model.playing_artist : "";
    float size=a->geometry.compact ? 40.0f : 56.0f;
    CLAY(CLAY_ID("PlayerMetadata"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_GROW()},
        .childGap=12,.childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
        CLAY(CLAY_ID("PlayerArtwork"),{.layout={.sizing={CLAY_SIZING_FIXED(size),CLAY_SIZING_FIXED(size)}},
            .backgroundColor=current && a->services.artwork ? (Clay_Color){0} : (Clay_Color){35,35,35,255},
            .custom={.customData=current && a->services.artwork ? &a->now.cover : NULL}}) { }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIT()},
            .childGap=4,.layoutDirection=CLAY_TOP_TO_BOTTOM},.clip={.horizontal=true}}) {
            app_label((Clay_String){false,(int32_t)strlen(title),title},14,(Clay_Color){245,245,245,255});
            if (*artist) { app_label((Clay_String){false,(int32_t)strlen(artist),artist},12,(Clay_Color){170,170,170,255}); }
            if (p->state==PLAYER_BUFFERING || p->state==PLAYER_LOADING || p->state==PLAYER_SEEKING || p->state==PLAYER_ERROR) {
                app_label(p->state==PLAYER_ERROR ? CLAY_STRING("Playback error") : p->state==PLAYER_BUFFERING ?
                    CLAY_STRING("Buffering...") : p->state==PLAYER_SEEKING ? CLAY_STRING("Seeking...") : CLAY_STRING("Loading..."),
                    12,(Clay_Color){170,170,170,255});
            }
        }
    }
}
static void modes(App *a)
{
    const PlayerSnapshot *p=&a->model.player;
    app_player_icon(a,CLAY_ID("PlayerShuffle"),UI_ICON_SHUFFLE,(AppAction){.type=APP_SHUFFLE},
        p->shuffled ? "Disable shuffle" : "Enable shuffle",unavailable(a),p->shuffled,false);
}
static void repeat(App *a)
{
    const PlayerSnapshot *p=&a->model.player;
    app_player_icon(a,CLAY_ID("PlayerRepeat"),p->repeat==PLAYER_REPEAT_ONE ? UI_ICON_REPEAT_ONE : UI_ICON_REPEAT,
        (AppAction){.type=APP_REPEAT},p->repeat==PLAYER_REPEAT_OFF ? "Repeat off; enable repeat one" :
        p->repeat==PLAYER_REPEAT_ONE ? "Repeat one; enable repeat all" : "Repeat all; disable repeat",unavailable(a),p->repeat!=PLAYER_REPEAT_OFF,false);
}
static void transport(App *a)
{
    const char *reason=unavailable(a); const PlayerSnapshot *p=&a->model.player;
    CLAY(CLAY_ID("PlayerTransport"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(32)},.childGap=8,
        .childAlignment={.x=CLAY_ALIGN_X_CENTER,.y=CLAY_ALIGN_Y_CENTER}}}) {
        if (!a->geometry.compact) { modes(a); }
        app_player_icon(a,CLAY_ID("PlayerPrevious"),UI_ICON_PREVIOUS,(AppAction){.type=APP_PREVIOUS},"Previous",reason,false,false);
        bool pause=p->state==PLAYER_PLAYING || p->state==PLAYER_BUFFERING || p->state==PLAYER_SEEKING || p->state==PLAYER_LOADING;
        app_player_icon(a,CLAY_ID("PlayerPlay"),pause ? UI_ICON_PAUSE : UI_ICON_PLAY,(AppAction){.type=APP_TOGGLE_PLAY},
            pause ? "Pause" : "Play",reason,false,true);
        app_player_icon(a,CLAY_ID("PlayerNext"),UI_ICON_NEXT,(AppAction){.type=APP_NEXT},"Next",reason,false,false);
        if (!a->geometry.compact) { repeat(a); }
    }
}
static void progress(App *a,float width)
{
    const PlayerSnapshot *p=&a->model.player; UiSeekSlider *slider=&a->widgets.seeks[0];
    bool seek=!unavailable(a) && p->duration_frames && (p->timeline_valid || p->state==PLAYER_PAUSED);
    uint64_t frames=slider->dragging ? (slider->value>=1 ? p->duration_frames :
        (uint64_t)(slider->value*(double)p->duration_frames)) : p->position_frames;
    uint64_t seconds=p->rate ? frames/p->rate : 0, duration=p->rate ? p->duration_frames/p->rate : 0;
    char elapsed[32], total[32];
    (void)snprintf(elapsed,sizeof(elapsed),"%llu:%02llu",(unsigned long long)(seconds/60),(unsigned long long)(seconds%60));
    (void)snprintf(total,sizeof(total),"%llu:%02llu",(unsigned long long)(duration/60),(unsigned long long)(duration%60));
    (void)snprintf(a->time,sizeof(a->time),"%s / %s",elapsed,total);
    CLAY(CLAY_ID("PlayerProgress"),{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(28)},.childGap=8,
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(36),CLAY_SIZING_FIT()},.childAlignment={.x=CLAY_ALIGN_X_RIGHT}}}) {
            app_label(app_text(a,(Clay_String){false,(int32_t)strlen(elapsed),elapsed}),12,(Clay_Color){170,170,170,255});
        }
        if (a->control_count<APP_CONTROLS) { a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("AppSeek"),.action={.type=APP_SEEK},.disabled=!seek}; }
        ui_level_slider(&a->widgets,CLAY_ID("AppSeek"),slider,fmaxf(16,width-88),!seek);
        app_label(app_text(a,(Clay_String){false,(int32_t)strlen(total),total}),12,(Clay_Color){170,170,170,255});
    }
    if (a->widgets.hover==CLAY_ID("AppSeek").id || a->widgets.focus==CLAY_ID("AppSeek").id) {
        a->now.tooltip=seek ? "Seek: drag, arrows 5 seconds, Home/End" : "Seek unavailable until the playback timeline is ready";
        a->now.tooltip_id=CLAY_ID("AppSeek");
    }
    double value;
    if (seek && ui_seek_take_commit(slider,&value)) { (void)app_enqueue(a,(AppAction){.type=APP_SEEK,.value=value}); }
    if (a->widgets.input.cancel || !seek) { slider->value=p->duration_frames ? fmin(1,(double)p->position_frames/(double)p->duration_frames) : 0; }
}
static void secondary(App *a)
{
    bool lyrics=a->model.logged_in && a->services.catalog && a->model.player.state!=PLAYER_STOPPED;
    app_player_icon(a,CLAY_ID("PlayerLyrics"),UI_ICON_LYRICS,(AppAction){.type=APP_GO_LYRICS},"Lyrics",
        lyrics ? NULL : "Lyrics requires a signed-in catalog service and a selected track",a->model.location.screen==APP_LYRICS,false);
    app_player_icon(a,CLAY_ID("PlayerQueue"),UI_ICON_QUEUE,(AppAction){.type=APP_QUEUE},"Playback queue",
        a->model.logged_in && (a->services.player || a->services.queue_snapshot) ? NULL : "Queue requires a playback queue service",a->now.queue_open,false);
    if (!a->geometry.compact && a->geometry.width>=1000) {
        app_player_icon(a,CLAY_ID("PlayerDevice"),UI_ICON_DEVICE,(AppAction){0},"Devices","Device transfer requires a bound Connect device service",false,false);
    }
    app_volume_controls(a);
    if (!a->geometry.compact && a->geometry.width>=1400) {
        app_player_icon(a,CLAY_ID("PlayerMini"),UI_ICON_MINIPLAYER,(AppAction){0},"Mini player","Mini player unavailable: no window service",false,false);
        app_player_icon(a,CLAY_ID("PlayerFullscreen"),UI_ICON_FULLSCREEN,(AppAction){0},"Fullscreen","Fullscreen unavailable: no window service",false,false);
    }
}
void app_now_playing(App *a)
{
    a->now.glyph_count=0; a->now.tooltip=NULL;
    float inner=a->geometry.width-32;
    float center=fminf(inner*0.40f,inner-448);
    CLAY(CLAY_ID("AppNowPlaying"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(a->geometry.bar)},
        .padding={8,8,4,4},.childGap=4,.layoutDirection=a->geometry.compact ? CLAY_TOP_TO_BOTTOM : CLAY_LEFT_TO_RIGHT,
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}},.backgroundColor={8,8,8,255}}) {
        if (a->geometry.compact) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(44)},.childGap=8}}) {
                metadata(a);
                CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(112),CLAY_SIZING_GROW()},.childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) { transport(a); }
            }
            progress(a,inner);
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(32)},.childGap=4,.childAlignment={.x=CLAY_ALIGN_X_CENTER}}}) {
                modes(a); repeat(a); secondary(a);
            }
        } else {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED((inner-center)*0.5f-4),CLAY_SIZING_GROW()}}}) { metadata(a); }
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(center),CLAY_SIZING_FIT()},.childGap=4,.layoutDirection=CLAY_TOP_TO_BOTTOM}}) {
                transport(a); progress(a,center);
            }
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_GROW()},.childGap=4,
                .childAlignment={.x=CLAY_ALIGN_X_RIGHT,.y=CLAY_ALIGN_Y_CENTER}}}) { secondary(a); }
        }
    }
    app_queue_panel(a); app_player_tooltip(a);
}
