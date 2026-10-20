#include "app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool equal(float a,float b) { return fabsf(a-b)<0.0001f; }
static void slider_sync(App *a)
{
    if (!a->widgets.seeks[1].dragging) { a->widgets.seeks[1].value=a->model.volume.value; }
}
static void cancel_drag(App *a)
{
    if (a->widgets.seeks[1].dragging) { (void)ui_seek_cancel(&a->widgets.seeks[1]); }
    if (a->widgets.active==CLAY_ID("AppVolume").id) { a->widgets.active=0; }
    slider_sync(a);
}
void app_volume_init(App *a)
{
    a->model.volume=(AppVolume){.value=1,.nonzero=1};
    a->model.player.volume=1; slider_sync(a);
}
bool app_volume_available(const App *a)
{
    return a && a->alive && a->model.logged_in && (a->services.player || a->services.submit);
}
void app_volume_session_end(App *a)
{
    AppVolume *v=&a->model.volume;
    v->pending_ticket=0; v->observed_valid=false; v->restore_pending=true; cancel_drag(a);
}
AppResult app_volume_set(App *a,double value)
{
    if (!isfinite(value)) { return APP_INVALID; }
    if (!app_volume_available(a)) { return APP_UNAVAILABLE; }
    AppVolume *v=&a->model.volume; float level=(float)fmin(1,fmax(0,value));
    if (!v->restore_pending && equal(level,v->value)) { return APP_OK; }
    if (v->serial==UINT64_MAX) { slider_sync(a); return APP_FULL; }
    PlayerCommand command={.type=PLAYER_VOLUME,.volume=level,.ticket=v->serial+1};
    PlayerResult result=a->services.player ? player_submit(a->services.player,&command) :
        a->services.submit(a->services.context,&command);
    if (result!=PLAYER_OK) {
        /* A continuous gesture never rolls back already accepted commands. */
        a->widgets.seeks[1].value=v->value;
        return result==PLAYER_FULL ? APP_FULL : APP_UNAVAILABLE;
    }
    v->serial=command.ticket; v->pending_ticket=command.ticket; v->value=level; v->restore_pending=false;
    if (level>0) { v->nonzero=level; }
    slider_sync(a); return APP_OK;
}
void app_volume_prepare(App *a)
{
    if (a->model.volume.restore_pending && app_volume_available(a)) {
        (void)app_volume_set(a,a->model.volume.value);
    }
}
void app_volume_snapshot(App *a,PlayerSnapshot snapshot)
{
    AppVolume *v=&a->model.volume; float level=snapshot.volume;
    if (v->restore_pending) { return; }
    if (v->pending_ticket && snapshot.volume_ticket!=v->pending_ticket) {
        bool intermediate=snapshot.volume_ticket && snapshot.volume_ticket<v->pending_ticket;
        bool unchanged=v->observed_valid && snapshot.volume_ticket==v->observed_ticket && equal(level,v->observed);
        if (!v->observed_valid || intermediate || unchanged) {
            v->observed=level; v->observed_ticket=snapshot.volume_ticket; v->observed_valid=true; return;
        }
    }
    bool changed=v->observed_valid && (snapshot.volume_ticket!=v->observed_ticket || !equal(level,v->observed));
    bool own_ack=v->pending_ticket && snapshot.volume_ticket==v->pending_ticket;
    v->pending_ticket=0; v->observed=level; v->observed_ticket=snapshot.volume_ticket; v->observed_valid=true; v->value=level;
    if (level>0) { v->nonzero=level; }
    if (changed && !own_ack) { cancel_drag(a); }
    slider_sync(a);
}
AppResult app_volume_key(App *a,AppKey key)
{
    if (!app_volume_available(a)) { return APP_UNAVAILABLE; }
    UiSeekSlider *slider=&a->widgets.seeks[1]; double value=slider->value;
    switch (key) {
    case APP_KEY_LEFT: case APP_KEY_DOWN: value-=0.01; break;
    case APP_KEY_RIGHT: case APP_KEY_UP: value+=0.01; break;
    case APP_KEY_HOME: value=0; break;
    case APP_KEY_END: value=1; break;
    default: return APP_CANCELLED;
    }
    if (slider->dragging) { return APP_CANCELLED; }
    value=fmin(1,fmax(0,value));
    AppResult result=app_enqueue(a,(AppAction){.type=APP_VOLUME,.value=value});
    if (result==APP_OK) { slider->value=value; }
    return result;
}
void app_volume_controls(App *a)
{
    bool enabled=app_volume_available(a); UiSeekSlider *slider=&a->widgets.seeks[1];
    if (!enabled) { cancel_drag(a); }
    slider_sync(a);
    (void)snprintf(a->volume,sizeof(a->volume),"Volume %u%% (arrows: 1%%; Home/End)",(unsigned)(slider->value*100+0.5));
    CLAY(CLAY_ID("AppVolumeControls"),{.layout={.sizing={CLAY_SIZING_FIT(),CLAY_SIZING_FIXED(32)},
        .childGap=4,.childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
        app_player_icon(a,CLAY_ID("PlayerMute"),a->model.volume.value>0 ? UI_ICON_VOLUME : UI_ICON_MUTE,
            (AppAction){.type=APP_MUTE},a->model.volume.value>0 ? "Mute" : "Unmute",
            enabled ? NULL : "Volume unavailable: sign in and bind a playback service",false,false);
        if (a->control_count<APP_CONTROLS) {
            a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("AppVolume"),.action={.type=APP_VOLUME},.disabled=!enabled};
        } else { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); }
        ui_level_slider(&a->widgets,CLAY_ID("AppVolume"),slider,112,!enabled);
        if (a->widgets.hover==CLAY_ID("AppVolume").id || a->widgets.focus==CLAY_ID("AppVolume").id) {
            a->now.tooltip=a->volume; a->now.tooltip_id=CLAY_ID("AppVolume");
        }
        double value;
        if (enabled && (ui_seek_take_commit(slider,&value) || ui_seek_take_preview(slider,&value)) &&
            fabs(value-a->model.volume.value)>0.0001) {
            (void)app_enqueue(a,(AppAction){.type=APP_VOLUME,.value=value});
        }
        if (a->widgets.input.cancel) { slider->value=a->model.volume.value; }
    }
}
