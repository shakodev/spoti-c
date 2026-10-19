#include "app_desktop.h"
#include <string.h>

static PlayerResult submit(void *context, const PlayerCommand *command)
{
    App *a=&((AppDesktop *)context)->app;
    if (!a->alive || (!a->model.logged_in && command->type!=PLAYER_STOP)) { return PLAYER_CLOSED; }
    return a->services.player ? player_submit(a->services.player,command) :
        a->services.submit ? a->services.submit(a->services.context,command) : PLAYER_CLOSED;
}
static void wake(void *context)
{
    AppDesktop *d=context; Event event={.type=UI_DIRTY_STATE}; (void)event_post(d->media_events,&event);
}
MediaResult app_desktop_media_open(AppDesktop *d, UiWindow *window)
{
    return app_desktop_media_bind(d,window,media_winrt_adapter(&d->media_native,window->hwnd));
}
MediaResult app_desktop_media_bind(AppDesktop *d, UiWindow *window, MediaAdapter adapter)
{
    d->media_events=window->config.workers;
    d->media_result=media_controls_init(&d->media,(MediaConfig){submit,wake,d},adapter);
    return d->media_result;
}
void app_desktop_media_prepare(AppDesktop *d, uint64_t now)
{
    if (!d->media.alive || !d->app.alive) { return; }
    AppModel *m=&d->app.model; MediaMetadata info={0}; memcpy(info.track_id,m->player.track_id,16);
    if (!memcmp(m->player.track_id,m->playing.id,16)) {
        memcpy(info.title,m->playing.title,sizeof(info.title));
        if (!memcmp(m->player.track_id,m->playing_artist_id,16)) { memcpy(info.artist,m->playing_artist,sizeof(info.artist)); }
        if (!memcmp(m->playing.id,m->playing_artwork_id,16) && !d->app.services.demo) {
            memcpy(info.artwork,m->playing_artwork,sizeof(info.artwork));
        }
    }
    uint32_t capabilities=m->logged_in && (d->app.services.player || d->app.services.submit) ?
        MEDIA_CAN_PLAY|MEDIA_CAN_PAUSE|MEDIA_CAN_STOP|MEDIA_CAN_NEXT|MEDIA_CAN_PREVIOUS|MEDIA_CAN_SEEK : 0;
    d->media_result=media_controls_publish(&d->media,&info,m->player,capabilities,now);
    MediaResult r=media_controls_pump(&d->media);
    if (r!=MEDIA_OK && r!=MEDIA_STALE) {
        d->media_result=r; m->last_result=r==MEDIA_FULL ? APP_FULL : APP_UNAVAILABLE;
    }
}
bool app_desktop_media_close(AppDesktop *d) { return media_controls_close(&d->media)==MEDIA_OK; }
