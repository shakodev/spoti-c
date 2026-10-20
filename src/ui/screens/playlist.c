#include "playlist.h"
#include <stdio.h>
#include <string.h>

static TrackResult change(Playlist *p)
{
    if (p->generation==UINT64_MAX) { return TRACK_FULL; }
    ++p->generation; p->pending=p->need_filter=true; p->play_key=0;
    p->selected_visible=p->selected_playable=false; p->first_playable=0;
    p->result=TRACK_PENDING;
    p->row_count=p->string_bytes=0; track_filter_cancel(p->filter); return TRACK_PENDING;
}
void playlist_init(Playlist *p, TrackFilter *filter)
{
    memset(p,0,sizeof(*p)); p->filter=filter;
}
void playlist_close(Playlist *p)
{
    if (!p) { return; }
    track_filter_cancel(p->filter); TrackFilter *filter=p->filter;
    (void)track_filter_clear(filter);
    uint64_t generation=p->generation==UINT64_MAX ? UINT64_MAX : p->generation+1;
    memset(p,0,sizeof(*p)); p->filter=filter; p->generation=generation;
}
TrackResult playlist_route(Playlist *p, const char *uri, uint64_t route)
{
    if (!p || !p->filter || !uri || strlen(uri)>=sizeof(p->uri)) { return TRACK_INVALID; }
    if (p->generation>=UINT64_MAX-1) { return TRACK_FULL; }
    playlist_close(p); memcpy(p->uri,uri,strlen(uri)+1); p->route=route; p->active=p->pending=true;
    p->coverage=TRACK_LOADED_ONLY; return TRACK_PENDING;
}
TrackResult playlist_source(Playlist *p, const TrackSource *source, uint64_t route)
{
    if (!p || !p->active || route!=p->route) { return TRACK_STALE; }
    if (!source || !source->read || !memchr(source->revision,0,64) || !source->revision[0] ||
        !memchr(source->uri,0,192) || strcmp(p->uri,source->uri)) { return TRACK_INVALID; }
    if (p->generation==UINT64_MAX) { return TRACK_FULL; }
    if (strcmp(source->revision,p->source.revision)) { p->selected=0; }
    p->source=*source; p->base=0; p->ready=false; return change(p);
}
TrackResult playlist_query(Playlist *p, const char *query, TrackSort sort)
{
    if (!p || !p->active || !query || sort<TRACK_SOURCE_ORDER || sort>TRACK_DURATION_ORDER) { return TRACK_INVALID; }
    size_t n=strlen(query); if (n>=TRACK_QUERY_BYTES) { return TRACK_FULL; }
    if (!track_utf8(query,n)) { return TRACK_INVALID; }
    if (!strcmp(p->query,query) && p->sort==sort) { return TRACK_OK; }
    if (p->generation==UINT64_MAX) { return TRACK_FULL; }
    memcpy(p->query,query,n+1); p->sort=sort; return change(p);
}
TrackResult playlist_window(Playlist *p, bool next)
{
    if (!p || !p->active || !p->source.read) { return TRACK_INVALID; }
    if ((next && p->source.total-p->base<=TRACK_INDEX_CAP) || (!next && !p->base)) { return TRACK_FULL; }
    if (p->generation==UINT64_MAX) { return TRACK_FULL; }
    p->base=next ? p->base+TRACK_INDEX_CAP : p->base-TRACK_INDEX_CAP; p->ready=false; return change(p);
}
const TrackRow *playlist_row(const Playlist *p, uint32_t ordinal)
{
    if (!p || p->cache_generation!=p->generation) { return NULL; }
    for (uint32_t i=0;i<p->row_count;++i) { if (p->rows[i].ordinal==ordinal) { return &p->rows[i]; } }
    return NULL;
}
static uint16_t copy(Playlist *p, const char *s)
{
    size_t n=strlen(s)+1;
    if (n>sizeof(p->strings)-p->string_bytes) { p->result=TRACK_FULL; return 0; }
    uint16_t at=(uint16_t)p->string_bytes; memcpy(p->strings+at,s,n); p->string_bytes+=(uint32_t)n; return at;
}
static void metadata(Playlist *p, const TrackJob *j)
{
    p->row_count=p->string_bytes=0; p->cache_generation=p->generation;
    for (uint32_t i=0;i<j->count;++i) {
        TrackRecord *record=&p->filter->batch[i]; TrackRow *row=&p->rows[p->row_count++];
        *row=(TrackRow){.ordinal=j->rows[i],.duration_ms=record->duration_ms,.flags=record->flags};
        char title[128];
        (void)snprintf(title,sizeof(title),"%s%s",record->title,record->flags&TRACK_PLAYABLE ? "" : " [Unavailable]");
        row->title=copy(p,record->flags&TRACK_METADATA ? title : "Metadata not loaded");
        row->artist=copy(p,record->artist); row->album=copy(p,record->album);
        char duration[32]; (void)snprintf(duration,sizeof(duration),"%u:%02u",record->duration_ms/60000,record->duration_ms/1000%60);
        row->time=copy(p,record->flags&TRACK_METADATA ? duration : "--:--");
    }
}
static void describe(Playlist *p)
{
    const char *coverage=p->coverage==TRACK_FULL_INDEX ? "Full index" : p->coverage==TRACK_WINDOW ? "Current 10k window" : "Loaded metadata only";
    (void)snprintf(p->summary,sizeof(p->summary),"%u results | %s | %u metadata missing%s",p->count,coverage,p->missing,
        p->pending ? " | Searching..." : "");
}
TrackResult playlist_prepare(Playlist *p, UiVirtualList *list, float viewport)
{
    if (!p || !p->filter || !list) { return TRACK_INVALID; }
    const TrackJob *done=track_filter_done(p->filter);
    if (done) {
        if (!p->active || done->generation!=p->generation || strcmp(done->source.revision,p->source.revision)) { ++p->stale_jobs; }
        else if (done->result!=TRACK_OK) {
            p->result=done->result;
            if (!done->metadata) { p->ready=p->pending=p->need_filter=false; }
        } else if (done->metadata) { metadata(p,done); }
        else {
            p->count=done->count; memcpy(p->map,p->filter->map,(size_t)p->count*sizeof(p->map[0]));
            p->coverage=done->coverage; p->missing=done->missing; p->result=TRACK_OK;
            p->first_playable=0;
            for (uint32_t i=0;i<p->count;++i) {
                const TrackIdentity *id=&p->filter->index[p->map[i]-p->base];
                if ((id->flags&(TRACK_ID|TRACK_PLAYABLE))==(TRACK_ID|TRACK_PLAYABLE)) {
                    p->first_playable=(uint64_t)p->map[i]+1; break;
                }
            }
            p->pending=p->need_filter=false; p->ready=true; list->scroll=0;
            list->selection=p->selected; uint32_t at;
            p->selected_visible=playlist_index(p,p->selected,&at);
            p->selected_playable=p->selected_visible && (p->filter->index[p->map[at]-p->base].flags&TRACK_PLAYABLE)!=0;
            list->focus=playlist_index(p,list->focus,&at) ? list->focus : p->count ? playlist_key(p,0) : 0;
        }
        track_filter_release(p->filter);
    }
    if (!p->active) { (void)track_filter_clear(p->filter); return TRACK_CANCELLED; }
    describe(p);
    if (!p->source.read || atomic_load_explicit(&p->filter->phase,memory_order_acquire)) { return TRACK_PENDING; }
    TrackJob job={.source=p->source,.generation=p->generation,.base=p->base,.sort=p->sort};
    if (p->need_filter) {
        memcpy(job.query,p->query,sizeof(job.query)); TrackResult result=track_filter_start(p->filter,&job);
        if (result!=TRACK_PENDING) { p->result=result; p->ready=p->pending=p->need_filter=false; } return result;
    }
    if (!p->ready || p->result!=TRACK_OK) { return p->result; }
    UiListRange range;
    if (ui_virtual_range(p->count,40,list->scroll,viewport,2,&range)!=UI_WIDGET_OK || range.end-range.first>TRACK_BATCH) { return TRACK_INVALID; }
    bool missing=false;
    for (uint32_t i=range.first;i<range.end;++i) {
        job.rows[job.count++]=p->map[i]; missing|=playlist_row(p,p->map[i])==NULL;
    }
    if (!missing) { return TRACK_OK; }
    job.metadata=true; TrackResult result=track_filter_start(p->filter,&job);
    if (result!=TRACK_PENDING) { p->result=result; } return result;
}
uint64_t playlist_key(void *context, uint32_t index)
{
    Playlist *p=context; return index<p->count ? (uint64_t)p->map[index]+1 : 0;
}
bool playlist_index(void *context, uint64_t key, uint32_t *index)
{
    Playlist *p=context; if (!key || key>UINT32_MAX || !index) { return false; }
    for (uint32_t i=0;i<p->count;++i) { if (p->map[i]==key-1) { *index=i; return true; } } return false;
}
TrackResult playlist_select(Playlist *p, uint64_t key, bool play)
{
    uint32_t at;
    if (!p || !p->ready || p->pending || !playlist_index(p,key,&at)) { return TRACK_STALE; }
    p->selected=key;
    p->selected_visible=true; p->selected_playable=(p->filter->index[p->map[at]-p->base].flags&TRACK_PLAYABLE)!=0;
    if (play && !p->selected_playable) { return TRACK_INVALID; }
    if (play) { p->play_key=key; p->play_generation=p->generation; } return TRACK_OK;
}
uint64_t playlist_play_target(const Playlist *p)
{
    if (!p || !p->active || !p->ready || p->pending || p->result!=TRACK_OK) { return 0; }
    return p->selected_visible && p->selected_playable ? p->selected : p->first_playable;
}
TrackResult playlist_queue(const Playlist *p, uint64_t key, PlaylistQueue *out)
{
    uint32_t at;
    if (!p || !out || !p->ready || p->pending || !playlist_index((void *)p,key,&at)) { return TRACK_STALE; }
    const TrackIdentity *selected=&p->filter->index[p->map[at]-p->base];
    if ((selected->flags&(TRACK_ID|TRACK_PLAYABLE))!=(TRACK_ID|TRACK_PLAYABLE)) { return TRACK_INVALID; }
    *out=(PlaylistQueue){.transfer={.play=true}}; memcpy(out->uri,p->source.uri,sizeof(out->uri));
    memcpy(out->revision,p->source.revision,sizeof(out->revision));
    for (;at<p->count && out->transfer.count<PLAYER_QUEUE_CAP;++at) {
        const TrackIdentity *id=&p->filter->index[p->map[at]-p->base];
        if ((id->flags&(TRACK_ID|TRACK_PLAYABLE))!=(TRACK_ID|TRACK_PLAYABLE)) { continue; }
        unsigned n=out->transfer.count++; memcpy(out->transfer.ids[n],id->id,16); out->ordinals[n]=p->map[at];
    }
    out->more=at<p->count || p->source.total-p->base>TRACK_INDEX_CAP || !p->source.complete; return TRACK_OK;
}
TrackResult playlist_take_play(Playlist *p, PlaylistQueue *queue)
{
    if (!p || !p->play_key) { return TRACK_CANCELLED; }
    uint64_t key=p->play_key; p->play_key=0;
    return p->play_generation==p->generation ? playlist_queue(p,key,queue) : TRACK_STALE;
}
