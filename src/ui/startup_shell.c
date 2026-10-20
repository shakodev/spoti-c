#include "startup.h"
#include "protocol/media_source.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static bool entity(StateCacheEntity item,AppEntity *out)
{
    const char *kind=item.kind==COLLECTION_PLAYLIST_ITEM ? "playlist" : item.kind==COLLECTION_ALBUM ? "album" :
        item.kind==COLLECTION_ARTIST ? "artist" : item.kind==COLLECTION_TRACK ? "track" : NULL;
    if (!kind) { return false; }
    char track[MEDIA_URI_BYTES]; if (media_gid_to_uri(item.id,track)!=MEDIA_OK) { return false; }
    *out=(AppEntity){.kind=(CollectionItemKind)item.kind}; memcpy(out->id,item.id,16);
    (void)snprintf(out->uri,sizeof(out->uri),"spotify:%s:%s",kind,track+14);
    memcpy(out->title,item.label,STATE_CACHE_LABEL); return true;
}
void startup_restore(App *a,const StateCache *s)
{
    if (!a || !a->alive || !s) { return; }
    a->cached=s; a->model.location.screen=(AppScreen)s->route;
    if (s->route==APP_SHELF || s->route==APP_LYRICS) { a->model.location.screen=APP_HOME; }
    StateCacheEntity route={.kind=s->route_kind}; memcpy(route.id,s->route_id,16);
    memcpy(route.label,"Cached selection",17);
    if ((s->route==APP_PLAYLIST || s->route==APP_ALBUM || s->route==APP_ARTIST) && !entity(route,&a->model.location.entity)) {
        a->model.location.screen=APP_HOME;
    }
    a->model.selected=a->model.location.entity; a->model.detail=s->detail!=0;
    a->model.player.volume=(float)s->volume/1000; a->model.status=APP_OFFLINE;
    a->model.volume=(AppVolume){.value=(float)s->volume/1000,
        .nonzero=(float)s->volume_nonzero/1000,.restore_pending=true};
    a->widgets.seeks[1].value=a->model.volume.value;
    a->widgets.lists[0].scroll=s->scroll; a->model.location.scroll=s->scroll;
}
void startup_cached_view(App *a)
{
    app_label(CLAY_STRING("Offline — recent library"),18,(Clay_Color){235,235,235,255});
    app_label(CLAY_STRING("Sign in to refresh or play. Saved items may be unavailable."),14,(Clay_Color){185,185,185,255});
    const StateCache *s=a->cached;
    if (!s || !s->count) { app_label(CLAY_STRING("No saved library items"),16,(Clay_Color){185,185,185,255}); return; }
    uint32_t rows=(uint32_t)fmax(1,floor(a->geometry.viewport/32)); if (rows>12) { rows=12; }
    uint32_t last=s->count>rows ? s->count-rows : 0;
    uint32_t first=(uint32_t)fmax(0,fmin(last,floor(a->widgets.lists[0].scroll/32)));
    a->widgets.lists[0].scroll=(double)first*32;
    for (uint32_t i=first;i<s->count && i-first<rows;++i) {
        app_label((Clay_String){false,(int32_t)strlen(s->entities[i].label),s->entities[i].label},16,(Clay_Color){210,210,210,255});
    }
}
void startup_capture(StateCache *s,const App *a,HWND hwnd)
{
    if (!s || !a || !a->alive) { return; }
    state_cache_defaults(s,STARTUP_EPOCH);
    if (a->cached) { *s=*a->cached; }
    WINDOWPLACEMENT placement={.length=sizeof(placement)};
    if (hwnd && GetWindowPlacement(hwnd,&placement)) {
        RECT r=placement.rcNormalPosition; s->x=r.left; s->y=r.top;
        s->width=(uint32_t)(r.right-r.left); s->height=(uint32_t)(r.bottom-r.top);
    }
    s->route=(uint32_t)a->model.location.screen; s->route_kind=(uint32_t)a->model.location.entity.kind;
    memcpy(s->route_id,a->model.location.entity.id,16); s->detail=a->model.detail ? 1u : 0;
    s->volume=(uint32_t)lroundf(fmaxf(0,fminf(1,a->model.volume.value))*1000);
    s->volume_nonzero=(uint32_t)lroundf(fmaxf(0.001f,fminf(1,a->model.volume.nonzero))*1000);
    s->scroll=(uint32_t)fmax(0,fmin(100000000,a->widgets.lists[0].scroll));
    if (a->model.logged_in) { memcpy(s->track_id,a->model.player.track_id,16); }
    if (a->model.logged_in && a->model.location.screen==APP_LIBRARY && a->model.page) {
        s->count=0; memset(s->entities,0,sizeof(s->entities));
        for (uint32_t i=0;i<a->model.page->count && s->count<STATE_CACHE_ENTITIES;++i) {
            const CollectionItem *item=&a->model.page->items[i];
            if (!item->has_compact_id || item->kind>COLLECTION_LIKED) { continue; }
            StateCacheEntity *e=&s->entities[s->count]; e->kind=(uint32_t)item->kind; memcpy(e->id,item->compact_id,16);
            if (json_base_string_copy(item->name,e->label,sizeof(e->label))!=JSON_OK) { memcpy(e->label,"Saved item",11); }
            for (size_t j=0;e->label[j];++j) { if ((uint8_t)e->label[j]<32) { e->label[j]=' '; } }
            ++s->count;
        }
    }
}
void startup_place(HWND hwnd,const StateCache *s)
{
    RECT r={s->x,s->y,s->x+(int)s->width,s->y+(int)s->height};
    HMONITOR monitor=MonitorFromRect(&r,MONITOR_DEFAULTTONEAREST); MONITORINFO info={.cbSize=sizeof(info)};
    if (!GetMonitorInfoW(monitor,&info)) { return; }
    int width=(int)s->width,height=(int)s->height;
    if (width>info.rcWork.right-info.rcWork.left) { width=info.rcWork.right-info.rcWork.left; }
    if (height>info.rcWork.bottom-info.rcWork.top) { height=info.rcWork.bottom-info.rcWork.top; }
    int x=s->x,y=s->y;
    if (x<info.rcWork.left || x+width>info.rcWork.right) { x=info.rcWork.left; }
    if (y<info.rcWork.top || y+height>info.rcWork.bottom) { y=info.rcWork.top; }
    (void)SetWindowPos(hwnd,NULL,x,y,width,height,SWP_NOACTIVATE|SWP_NOZORDER);
}
