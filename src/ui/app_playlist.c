#include "app.h"
#include "screens/playlist.h"
#include <math.h>
#include <string.h>

void app_refresh_clock(App *a, uint32_t dirty)
{
    if (!a || !a->alive || !a->model.logged_in || !(dirty&(UI_DIRTY_PROGRESS|UI_DIRTY_LYRICS))) { return; }
    if (a->services.player) { (void)player_refresh_clock(a->services.player); }
    else if (a->services.refresh_clock) { (void)a->services.refresh_clock(a->services.context); }
}

void app_playlist_prepare(App *a)
{
    Playlist *p=a->services.playlist;
    bool changed=track_filter_done(p->filter)!=NULL;
    TrackResult r=playlist_prepare(p,&a->widgets.lists[0],fminf(360,fmaxf(0,a->geometry.viewport-104)));
    if (changed) { Clay_SetCurrentContext(a->widgets.clay); Clay_ResetMeasureTextCache(); }
    if (a->model.location.screen!=APP_PLAYLIST || !p->active || p->route!=a->model.generation) { return; }
    if (p->source.read) {
        a->model.pending=false;
        a->model.status=p->pending ? APP_LOADING : p->ready ? p->count ? APP_CONTENT : APP_EMPTY : APP_ERROR;
    }
    if (r!=TRACK_OK && r!=TRACK_PENDING && r!=TRACK_CANCELLED) { a->model.last_result=APP_FAILED; }
    PlaylistQueue queue;
    r=playlist_take_play(p,&queue);
    if (r==TRACK_CANCELLED) { return; }
    if (r!=TRACK_OK || !a->model.logged_in) { a->model.last_result=APP_UNAVAILABLE; return; }
    PlayerCommand command={.type=PLAYER_TRANSFER,.transfer=&queue.transfer};
    PlayerResult result=a->services.playlist_queue ? a->services.playlist_queue(a->services.context,&queue) :
        a->services.player ? player_submit(a->services.player,&command) : a->services.submit ?
        a->services.submit(a->services.context,&command) : PLAYER_CLOSED;
    a->model.last_result=result==PLAYER_OK ? APP_OK : result==PLAYER_FULL ? APP_FULL : APP_FAILED;
    if (result==PLAYER_OK) {
        const TrackRow *row=playlist_row(p,queue.ordinals[0]); a->model.playing=(AppEntity){.kind=COLLECTION_TRACK,.playable=true};
        memcpy(a->model.playing.id,queue.transfer.ids[0],16);
        const char *title=row ? p->strings+row->title : "Playlist track";
        memcpy(a->model.playing.title,title,strlen(title)+1);
        const char *artist=row ? p->strings+row->artist : "";
        strcpy_s(a->model.playing_artist,sizeof(a->model.playing_artist),artist);
        memcpy(a->model.playing_artist_id,a->model.playing.id,16);
        for (uint32_t remaining=queue.transfer.count;remaining;--remaining) {
            uint32_t i=remaining-1; /* Keep the current and nearest upcoming IDs in the bounded cache. */
            const TrackRow *item=playlist_row(p,queue.ordinals[i]);
            if (!item) { continue; }
            AppEntity entity={.kind=COLLECTION_TRACK,.playable=true};
            memcpy(entity.id,queue.transfer.ids[i],16);
            strcpy_s(entity.title,sizeof(entity.title),p->strings+item->title);
            (void)app_track_remember(a,&entity,p->strings+item->artist,"");
        }
    }
}
