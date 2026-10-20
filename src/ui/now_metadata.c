#include "app.h"
#include "track_filter.h"
#include "protocol/media_source.h"
#include <string.h>

static bool valid(const char *s,size_t cap)
{
    const char *end=s ? memchr(s,0,cap) : NULL;
    return end && track_utf8(s,(size_t)(end-s));
}
static AppTrack *find(App *a,const uint8_t id[16])
{
    for (unsigned i=0;i<8;++i) {
        if (a->now.tracks[i].used && !memcmp(a->now.tracks[i].entity.id,id,16)) { return &a->now.tracks[i]; }
    }
    return NULL;
}
AppResult app_track_remember(App *a,const AppEntity *entity,const char *artist,const char *artwork)
{
    if (!a || !entity || a->widgets.in_frame || !valid(entity->title,sizeof(entity->title)) ||
        !valid(entity->uri,sizeof(entity->uri)) || !valid(artist,192) || !valid(artwork,512)) { return APP_INVALID; }
    AppTrack *track=find(a,entity->id);
    if (!track) {
        track=&a->now.tracks[0];
        for (unsigned i=1;i<8;++i) { if (a->now.tracks[i].used<track->used) { track=&a->now.tracks[i]; } }
        memset(track,0,sizeof(*track));
    }
    track->entity=*entity;
    if (!track->entity.uri[0]) { (void)media_gid_to_uri(entity->id,track->entity.uri); }
    if (*artist) { strcpy_s(track->artist,sizeof(track->artist),artist); }
    if (*artwork) { strcpy_s(track->artwork,sizeof(track->artwork),artwork); }
    track->used=++a->now.serial; return APP_OK;
}
static void resolved(App *a,const uint8_t id[16])
{
    MediaDisplay display={0};
    if (!a->services.track_info || !a->services.track_info(a->services.context,id,&display)) { return; }
    AppEntity entity={.kind=COLLECTION_TRACK,.playable=true}; char artwork[512]={0};
    memcpy(entity.id,id,16); strcpy_s(entity.title,sizeof(entity.title),display.title[0] ? display.title : "Untitled");
    if (display.has_cover) {
        /* Pinned librespot/cspot source evidence: docs/specs/now-playing-bar.md. */
        const char prefix[]="https://i.scdn.co/image/", digits[]="0123456789abcdef";
        memcpy(artwork,prefix,sizeof(prefix)-1);
        for (unsigned i=0;i<20;++i) {
            artwork[sizeof(prefix)-1+i*2]=digits[display.cover_id[i]>>4];
            artwork[sizeof(prefix)+i*2]=digits[display.cover_id[i]&15];
        }
    }
    (void)app_track_remember(a,&entity,display.artist,artwork);
}
void app_now_prepare(App *a)
{
    if (!a->model.logged_in) { return; }
    const uint8_t *id=a->model.player.track_id;
    resolved(a,id); AppTrack *track=find(a,id);
    if (track) {
        a->model.playing=track->entity;
        strcpy_s(a->model.playing_artist,sizeof(a->model.playing_artist),track->artist);
        strcpy_s(a->model.playing_artwork,sizeof(a->model.playing_artwork),track->artwork);
        memcpy(a->model.playing_artist_id,id,16); memcpy(a->model.playing_artwork_id,id,16);
    }
    a->now.cover=(AppImage){.tag=APP_CUSTOM_IMAGE,.id={.size=56,.generation=a->model.generation}};
    memcpy(a->now.cover.id.content,id,16);
    if (a->services.artwork && !memcmp(a->model.playing_artwork_id,id,16) && a->model.playing_artwork[0]) {
        wchar_t url[512];
        if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,a->model.playing_artwork,-1,url,512)) {
            ArtworkSource source={.url=url,.width=64,.height=64};
            (void)artwork_request(a->services.artwork,a->now.cover.id,&source,1);
            (void)artwork_lookup(a->services.artwork,a->now.cover.id,&a->now.cover.handle);
        }
    }
    if (a->now.queue_open) {
        PlayerQueueSnapshot queue={0};
        if (a->services.player) { player_queue_snapshot(a->services.player,&queue); }
        else if (!a->services.queue_snapshot || !a->services.queue_snapshot(a->services.context,&queue)) { queue.count=0; }
        if (queue.count<=PLAYER_QUEUE_CAP && (!queue.count || queue.index<queue.count)) { a->now.queue=queue; }
        if (a->now.queue_page>=a->now.queue.count) { a->now.queue_page=0; }
    }
}
