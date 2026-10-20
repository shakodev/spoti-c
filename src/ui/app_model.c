#include "app.h"
#include "screens/playlist.h"
#include <math.h>
#include <string.h>

static void dirty(App *a)
{
    if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_STATE); }
    if (a->services.wake) { a->services.wake(a->services.context); }
}
static void release_page(App *a)
{
    if (a->model.page) { (void)page_release((JsonPage *)&a->model.page->json); a->model.page = NULL; }
}
static AppResult generation(App *a)
{
    if (a->model.generation == UINT64_MAX) { return APP_FULL; }
    if (a->model.pending && a->services.cancel) { a->services.cancel(a->services.context,&a->model.request); }
    a->model.pending = false; release_page(a); app_catalog_clear(a); ++a->model.generation;
    if (!a->model.auth_pending) { a->model.notice[0]=0; }
    if (a->services.retire) { a->services.retire(a->services.context,a->model.generation); }
    if (a->services.playlist) { playlist_close(a->services.playlist); }
    (void)ui_text_set(&a->widgets.texts[0],"",0);
    memset(a->images,0,sizeof(a->images)); a->image_count = 0;
    for (uint32_t i=0;i<UI_WIDGET_SEEKS;++i) {
        if (a->widgets.seeks[i].dragging) { (void)ui_seek_cancel(&a->widgets.seeks[i]); }
    }
    a->widgets.active = 0; a->widgets.lists[0] = (UiVirtualList){0};
    if (a->services.artwork) { (void)artwork_navigate(a->services.artwork,a->model.generation); }
    return APP_OK;
}
static AppResult request(App *a, uint32_t cursor)
{
    AppModel *m = &a->model;
    if (m->pending) { return APP_PENDING; }
    if (!m->logged_in) { m->status = APP_SIGNED_OUT; return APP_UNAVAILABLE; }
    if (!m->online) { m->status = APP_OFFLINE; return APP_UNAVAILABLE; }
    if (m->location.screen == APP_SETTINGS) { m->status = APP_CONTENT; return APP_OK; }
    if (app_is_catalog(a)) { return app_catalog_request(a,cursor); }
    if (m->location.screen == APP_PLAYLIST && (!a->services.playlist ||
        playlist_route(a->services.playlist,m->location.entity.uri,m->generation)!=TRACK_PENDING)) { m->status=APP_ERROR; return APP_UNAVAILABLE; }
    if (m->serial == UINT64_MAX) { return APP_FULL; }
    m->request = (AppRequest){.generation=m->generation,.serial=++m->serial,.cursor=cursor,
        .screen=m->location.screen,.kind=m->location.screen == APP_HOME ? COLLECTION_HOME :
        m->location.screen == APP_SHELF ? COLLECTION_HOME_SECTION :
        m->location.screen == APP_PLAYLIST ? COLLECTION_PLAYLIST : COLLECTION_LIBRARY};
    memcpy(m->request.uri,m->location.entity.uri,sizeof(m->request.uri));
    AppResult r = a->services.request ? a->services.request(a->services.context,&m->request) : APP_UNAVAILABLE;
    m->pending = r == APP_PENDING;
    if (!m->page) { m->status = m->pending ? APP_LOADING : APP_ERROR; }
    return r;
}
static AppResult navigate(App *a, AppLocation location, bool back)
{
    if (a->model.generation == UINT64_MAX) { return APP_FULL; }
    if (!back && a->model.history_count == APP_HISTORY) { return APP_FULL; }
    if (!back) {
        a->model.location.scroll = a->widgets.lists[0].scroll;
        a->model.history[a->model.history_count++] = a->model.location;
    }
    (void)generation(a); a->model.location = location;
    a->widgets.lists[0].scroll = location.scroll; a->widgets.focus = 0;
    if (location.screen==APP_SEARCH) { (void)ui_text_set(&a->widgets.texts[0],location.query,(uint32_t)strlen(location.query)); }
    a->model.selected = location.entity;
    return request(a,0);
}
AppResult app_refresh(App *a)
{
    if (generation(a)!=APP_OK) { return APP_FULL; }
    if (a->model.location.screen==APP_SEARCH) {
        (void)ui_text_set(&a->widgets.texts[0],a->model.location.query,(uint32_t)strlen(a->model.location.query));
    }
    return request(a,0);
}
AppResult app_entity(const CollectionItem *item, AppEntity *out)
{
    if (!item || !out) { return APP_INVALID; }
    *out = (AppEntity){.kind=item->kind};
    if (json_base_string_copy(item->uri,out->uri,sizeof(out->uri)) != JSON_OK ||
        json_base_string_copy(item->name,out->title,sizeof(out->title)) != JSON_OK) { return APP_FULL; }
    if (!out->title[0]) { memcpy(out->title,"Untitled",9); }
    out->playable = item->available && item->kind == COLLECTION_TRACK && item->has_compact_id;
    memcpy(out->id,item->compact_id,16); return APP_OK;
}
AppResult app_publish_player(App *a, PlayerSnapshot snapshot, uint64_t now)
{
    if (!a || !a->alive || snapshot.generation < a->model.player.generation) { return APP_STALE; }
    if (!isfinite(snapshot.volume) || snapshot.volume < 0 || snapshot.volume > 1 ||
        snapshot.state > PLAYER_ERROR || snapshot.rate > 384000 || snapshot.repeat>PLAYER_REPEAT_ALL ||
        snapshot.queue_count>PLAYER_QUEUE_CAP || (snapshot.queue_count && snapshot.queue_index>=snapshot.queue_count)) { return APP_INVALID; }
    if (snapshot.generation!=a->model.player.generation && a->widgets.seeks[0].dragging) {
        (void)ui_seek_cancel(&a->widgets.seeks[0]);
        if (a->widgets.active==CLAY_ID("AppSeek").id) { a->widgets.active=0; }
    }
    if (memcmp(snapshot.track_id,a->model.player.track_id,16)) {
        if (memcmp(snapshot.track_id,a->model.playing.id,16)) {
            a->model.playing = (AppEntity){0}; a->model.playing_artwork[0]=0; a->model.playing_artist[0]=0;
        }
        a->widgets.seeks[0] = (UiSeekSlider){0};
    }
    PlayerSnapshot before=a->model.player;
    app_volume_snapshot(a,snapshot);
    a->model.player = snapshot;
    if (!a->widgets.seeks[0].dragging) {
        a->widgets.seeks[0].value = snapshot.duration_frames ?
            fmin(1,(double)snapshot.position_frames / (double)snapshot.duration_frames) : 0;
    }
    if (a->scheduler) { ui_scheduler_playing(a->scheduler,snapshot.state == PLAYER_PLAYING,now); }
    if (before.state!=snapshot.state || before.generation!=snapshot.generation || before.volume!=snapshot.volume ||
        before.volume_ticket!=snapshot.volume_ticket || before.repeat!=snapshot.repeat || before.shuffled!=snapshot.shuffled ||
        before.queue_count!=snapshot.queue_count || before.queue_index!=snapshot.queue_index ||
        before.rate!=snapshot.rate || before.duration_frames!=snapshot.duration_frames || before.error!=snapshot.error ||
        before.timeline_valid!=snapshot.timeline_valid || memcmp(before.track_id,snapshot.track_id,16)) { dirty(a); }
    return APP_OK;
}
static AppResult command(App *a, PlayerCommand c)
{
    if (!a->model.logged_in && c.type != PLAYER_STOP) { return APP_UNAVAILABLE; }
    PlayerResult r = a->services.player ? player_submit(a->services.player,&c) : a->services.submit ?
        a->services.submit(a->services.context,&c) : PLAYER_CLOSED;
    return r == PLAYER_OK ? APP_OK : r == PLAYER_FULL ? APP_FULL : APP_FAILED;
}
static AppResult track_result(TrackResult r)
{
    return r==TRACK_OK ? APP_OK : r==TRACK_PENDING ? APP_PENDING : r==TRACK_FULL ? APP_FULL :
        r==TRACK_STALE ? APP_STALE : r==TRACK_INVALID ? APP_INVALID : r==TRACK_CANCELLED ? APP_CANCELLED : APP_FAILED;
}
AppResult app_session(App *a, bool logged_in, bool online, uint64_t now)
{
    if (!a || !a->alive) { return APP_INVALID; }
    a->model.auth_pending = false;
    a->catalog.now=now;
    bool changed = a->model.logged_in != logged_in || a->model.online != online ||
        (logged_in && a->model.status == APP_AUTH_EXPIRED);
    a->model.logged_in = logged_in; a->model.online = online;
    if (!changed) { dirty(a); return APP_OK; }
    AppResult r = generation(a);
    if (r != APP_OK) { return r; }
    if (!logged_in) {
        a->cached=NULL;
        memset(&a->now,0,sizeof(a->now));
        a->model.history_count = 0; memset(a->model.history,0,sizeof(a->model.history));
        a->model.selected = a->model.playing = (AppEntity){0};
        memset(a->model.playing_artwork,0,sizeof(a->model.playing_artwork));
        memset(a->model.playing_artist,0,sizeof(a->model.playing_artist));
        memset(a->model.playing_artist_id,0,sizeof(a->model.playing_artist_id));
        memset(a->model.playing_artwork_id,0,sizeof(a->model.playing_artwork_id));
        a->model.location = (AppLocation){.screen=APP_HOME};
        app_volume_session_end(a);
        a->model.player = (PlayerSnapshot){.volume=a->model.volume.value};
        memset(a->controls,0,sizeof(a->controls)); a->control_count=0;
        memset(a->labels,0,sizeof(a->labels)); a->label_count=0;
        memset(a->measures,0,sizeof(a->measures)); a->measure_count=0;
        memset(a->actions,0,sizeof(a->actions)); a->action_count=0;
        a->widgets.focus=0;
        if (a->fonts) { (void)font_cache_reset(a->fonts->cache); }
        if (a->scheduler) { ui_scheduler_playing(a->scheduler,false,now); }
    }
    r = request(a,0); a->model.last_result=r; dirty(a); return r;
}
AppResult app_deliver(App *a, AppRequest delivery, CollectionResult result, const CollectionPage *page)
{
    AppResult r = APP_OK;
    if (!a || !a->alive || !a->model.pending || delivery.generation != a->model.generation ||
        delivery.serial != a->model.request.serial || delivery.kind != a->model.request.kind ||
        delivery.cursor != a->model.request.cursor) { r = APP_STALE; }
    else {
        a->model.pending = false;
        if (page && (result == COLLECTION_OK || result == COLLECTION_PARTIAL || result == COLLECTION_BOUNDARY) &&
            page->json.generation == delivery.generation && page->kind == delivery.kind && page->offset == delivery.cursor &&
            page->count <= COLLECTION_ROW_CAP && (page->next == COLLECTION_END || page->next > delivery.cursor)) {
            release_page(a); a->model.page = page; page = NULL;
            a->model.status = a->model.page->loading ? APP_LOADING : a->model.page->count ? APP_CONTENT : APP_EMPTY;
            a->model.last_result=APP_OK;
            a->widgets.lists[0].scroll = a->model.location.scroll;
            if (delivery.cursor) { a->widgets.lists[0].scroll = 0; }
        } else {
            r = APP_FAILED;
            a->model.status = result == COLLECTION_AUTH ? APP_AUTH_EXPIRED :
                !a->model.online ? APP_OFFLINE : APP_ERROR;
            if (result == COLLECTION_AUTH) { release_page(a); a->image_count = 0; }
        }
    }
    if (page) { (void)page_release((JsonPage *)&page->json); }
    if (a && a->alive) { if (r == APP_STALE) { ++a->stale_deliveries; } dirty(a); }
    return r;
}
AppResult app_enqueue(App *a, AppAction action)
{
    if (!a || !a->alive || action.type < APP_GO_HOME || action.type > APP_QUEUE_PAGE ||
        (action.type==APP_VOLUME && !isfinite(action.value)) ||
        ((action.type==APP_SELECT || action.type==APP_PLAYER_INFO) && (!memchr(action.entity.uri,0,sizeof(action.entity.uri)) ||
        !memchr(action.entity.title,0,sizeof(action.entity.title))))) { return APP_INVALID; }
    if (action.type==APP_VOLUME && a->action_count && a->actions[a->action_count-1].type==APP_VOLUME) {
        a->actions[a->action_count-1]=action; dirty(a); return APP_OK;
    }
    if (action.type==APP_SEEK) {
        action.generation=a->model.player.generation;
        memcpy(action.entity.id,a->model.player.track_id,16);
    }
    if (a->action_count == APP_ACTIONS) { a->model.last_result = APP_FULL; dirty(a); return APP_FULL; }
    a->actions[a->action_count++] = action; dirty(a); return APP_OK;
}
static AppResult apply(App *a, AppAction action, uint64_t now)
{
    AppModel *m = &a->model;
    switch (action.type) {
    case APP_SHUFFLE: return command(a,(PlayerCommand){.type=PLAYER_SHUFFLE,.value=m->player.shuffled ? 0 : 1});
    case APP_REPEAT: return command(a,(PlayerCommand){.type=PLAYER_REPEAT,.value=(m->player.repeat+1u)%3u});
    case APP_QUEUE:
        if (!m->logged_in || (!a->services.player && !a->services.queue_snapshot)) { return APP_UNAVAILABLE; }
        a->now.queue_open=!a->now.queue_open; a->now.queue_page=0; return APP_OK;
    case APP_QUEUE_PAGE:
        if (!isfinite(action.value) || action.value<0 || action.value>=PLAYER_QUEUE_CAP || floor(action.value)!=action.value) { return APP_INVALID; }
        a->now.queue_page=(uint32_t)action.value; return APP_OK;
    case APP_PLAYER_INFO:
        return app_set_notice(a,APP_INFORMATION,(Clay_String){false,(int32_t)strlen(action.entity.title),action.entity.title});
    case APP_READ_MESSAGES: return app_messages_request(a);
    case APP_GO_HOME: return navigate(a,(AppLocation){.screen=APP_HOME},false);
    case APP_GO_LIBRARY: return navigate(a,(AppLocation){.screen=APP_LIBRARY},false);
    case APP_GO_SETTINGS: return navigate(a,(AppLocation){.screen=APP_SETTINGS},false);
    case APP_GO_SEARCH: return navigate(a,(AppLocation){.screen=APP_SEARCH},false);
    case APP_GO_LYRICS: return navigate(a,(AppLocation){.screen=APP_LYRICS},false);
    case APP_BACK:
        if (!m->history_count) { return APP_CANCELLED; }
        return navigate(a,m->history[--m->history_count],true);
    case APP_RETRY:
        if (m->pending) { return APP_PENDING; }
        return app_refresh(a);
    case APP_MORE:
        if (app_is_catalog(a)) { return a->catalog.page && a->catalog.page->next!=CATALOG_END ?
            app_catalog_fetch(a,a->catalog.page->next) : APP_CANCELLED; }
        return m->page && m->page->next != COLLECTION_END ? request(a,m->page->next) : APP_CANCELLED;
    case APP_SIGN_IN: case APP_SIGN_OUT: {
        bool logout = action.type == APP_SIGN_OUT;
        if (m->auth_pending && !logout) { return APP_PENDING; }
        if (m->auth_serial==UINT64_MAX) { return APP_FULL; }
        ++m->auth_serial;
        if (logout) { (void)command(a,(PlayerCommand){.type=PLAYER_STOP}); (void)app_session(a,false,m->online,now); }
        AppResult r = a->services.auth ? a->services.auth(a->services.context,logout ? APP_LOGOUT : APP_LOGIN) : APP_UNAVAILABLE;
        m->auth_pending = r == APP_PENDING; return r;
    }
    case APP_SELECT:
        if (!m->logged_in) { return APP_UNAVAILABLE; }
        m->selected = action.entity;
        if (action.entity.kind==COLLECTION_ALBUM || action.entity.kind==COLLECTION_ARTIST) {
            return navigate(a,(AppLocation){.screen=action.entity.kind==COLLECTION_ALBUM ? APP_ALBUM : APP_ARTIST,
                .entity=action.entity,.selector=action.entity.kind==COLLECTION_ARTIST ? ARTIST_TOP_TRACKS : 0},false);
        }
        if (action.entity.kind == COLLECTION_SHELF) { return navigate(a,(AppLocation){.screen=APP_SHELF,.entity=action.entity},false); }
        if (action.entity.kind == COLLECTION_PLAYLIST_ITEM || action.entity.kind == COLLECTION_LIKED) {
            return navigate(a,(AppLocation){.screen=APP_PLAYLIST,.entity=action.entity},false);
        }
        if (action.entity.playable) {
            PlayerCommand c = {.type=PLAYER_LOAD,.play=true}; memcpy(c.track_id,action.entity.id,16);
            AppResult r = command(a,c);
            if (r == APP_OK) {
                m->playing = action.entity;
                (void)app_track_remember(a,&action.entity,"","");
            }
            return r;
        }
        m->detail = true; return APP_OK;
    case APP_TOGGLE_PLAY: return command(a,(PlayerCommand){.type=m->player.state==PLAYER_PLAYING ||
        m->player.state==PLAYER_LOADING || m->player.state==PLAYER_BUFFERING || m->player.state==PLAYER_SEEKING ? PLAYER_PAUSE : PLAYER_PLAY});
    case APP_PREVIOUS: return command(a,(PlayerCommand){.type=PLAYER_PREVIOUS});
    case APP_NEXT: return command(a,(PlayerCommand){.type=PLAYER_NEXT});
    case APP_SEEK:
        if (action.generation!=m->player.generation || memcmp(action.entity.id,m->player.track_id,16)) { return APP_STALE; }
        if (!isfinite(action.value) || !m->player.duration_frames ||
            (!m->player.timeline_valid && m->player.state != PLAYER_PAUSED)) { return APP_INVALID; }
        if (action.value >= 1) { return command(a,(PlayerCommand){.type=PLAYER_SEEK,.position_frames=m->player.duration_frames}); }
        return command(a,(PlayerCommand){.type=PLAYER_SEEK,.position_frames=(uint64_t)
            (fmax(0,action.value) * (double)m->player.duration_frames)});
    case APP_VOLUME:
        return app_volume_set(a,action.value);
    case APP_MUTE:
        return app_volume_set(a,m->volume.value>0 ? 0 : m->volume.nonzero);
    case APP_DETAIL: m->detail = !m->detail; return APP_OK;
    case APP_CATALOG_QUERY: case APP_CATALOG_SECTION: {
        if (m->location.screen!=APP_SEARCH && m->location.screen!=APP_ARTIST) { return APP_INVALID; }
        AppLocation location=m->location;
        if (action.type==APP_CATALOG_QUERY) {
            if (m->location.screen!=APP_SEARCH || a->widgets.texts[0].length>=APP_QUERY_BYTES) { return APP_FULL; }
            memcpy(location.query,a->widgets.texts[0].text,a->widgets.texts[0].length+1);
        } else {
            uint32_t limit=location.screen==APP_SEARCH ? SEARCH_ARTISTS : ARTIST_RELATED;
            if (!isfinite(action.value) || action.value<0 || action.value>limit || floor(action.value)!=action.value) { return APP_INVALID; }
            location.selector=(uint32_t)action.value;
        }
        location.scroll=0; m->location=location; return app_refresh(a);
    }
    case APP_CATALOG_SELECT:
        if (!isfinite(action.value) || action.value<0 || action.value>=CATALOG_PAGE_LIMIT || floor(action.value)!=action.value) { return APP_INVALID; }
        return app_catalog_activate(a,(uint32_t)action.value);
    case APP_LYRICS_FOLLOW: a->catalog.hold=CORE_CLOCK_NEVER; return APP_OK;
    case APP_TRACK_QUERY: case APP_TRACK_SORT:
        if (!a->services.playlist || m->location.screen!=APP_PLAYLIST ||
            (action.type==APP_TRACK_SORT && (!isfinite(action.value) || action.value<0 || action.value>TRACK_DURATION_ORDER))) { return APP_INVALID; }
        return track_result(playlist_query(a->services.playlist,a->widgets.texts[0].text,
            action.type==APP_TRACK_SORT ? (TrackSort)(int)action.value : a->services.playlist->sort));
    case APP_TRACK_PLAY:
        if (!a->services.playlist || m->location.screen!=APP_PLAYLIST || !isfinite(action.value) || action.value<1 || action.value>UINT32_MAX) { return APP_INVALID; }
        return track_result(playlist_select(a->services.playlist,(uint64_t)action.value,true));
    case APP_TRACK_WINDOW:
        if (!a->services.playlist || m->location.screen!=APP_PLAYLIST) { return APP_INVALID; }
        return track_result(playlist_window(a->services.playlist,action.value>0));
    default: return APP_INVALID;
    }
}
AppResult app_update(App *a, uint64_t now)
{
    if (!a || !a->alive || a->widgets.in_frame) { return APP_INVALID; }
    a->catalog.now=now;
    app_volume_prepare(a);
    /* Retire gestures against external changes before dispatching queued input. */
    if (a->model.logged_in) {
        if (a->services.player) { (void)app_publish_player(a,player_snapshot(a->services.player),now); }
        else if (a->services.snapshot) { (void)app_publish_player(a,a->services.snapshot(a->services.context),now); }
    }
    AppAction batch[APP_ACTIONS]; uint32_t count=a->action_count;
    memcpy(batch,a->actions,(size_t)count*sizeof(batch[0]));
    a->action_count=0; memset(a->actions,0,sizeof(a->actions));
    for (uint32_t i = 0; i < count; ++i) {
        AppAction action=batch[i]; a->model.last_result = apply(a,action,now);
        if (action.type==APP_SIGN_OUT) { a->action_count=0; memset(a->actions,0,sizeof(a->actions)); break; }
    }
    memset(batch,0,sizeof(batch));
    if (a->services.playlist) { app_playlist_prepare(a); }
    if (a->model.logged_in) {
        if (a->services.player) { (void)app_publish_player(a,player_snapshot(a->services.player),now); }
        else if (a->services.snapshot) { (void)app_publish_player(a,a->services.snapshot(a->services.context),now); }
    }
    if (a->services.artwork) { (void)artwork_begin_visible(a->services.artwork); }
    app_catalog_prepare(a,now);
    app_now_prepare(a);
    if (a->services.artwork) {
        for (uint32_t i = 0; i < a->image_count; ++i) {
            (void)artwork_lookup(a->services.artwork,a->images[i].id,&a->images[i].handle);
        }
        (void)artwork_prepare(a->services.artwork);
    }
    return a->model.last_result;
}
void app_dispose(App *a)
{
    if (!a || !a->alive || a->widgets.in_frame) { return; }
    (void)generation(a); release_page(a); (void)ui_widgets_dispose(&a->widgets);
    a->alive = false; memset(&a->model,0,sizeof(a->model)); a->control_count = a->action_count = 0;
}
