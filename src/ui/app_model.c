#include "app.h"
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
    a->model.pending = false; release_page(a); ++a->model.generation;
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
    if (m->serial == UINT64_MAX) { return APP_FULL; }
    m->request = (AppRequest){.generation=m->generation,.serial=++m->serial,.cursor=cursor,
        .kind=m->location.screen == APP_HOME ? COLLECTION_HOME :
        m->location.screen == APP_SHELF ? COLLECTION_HOME_SECTION : COLLECTION_LIBRARY};
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
    a->model.selected = location.entity;
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
        snapshot.state > PLAYER_ERROR || snapshot.rate > 384000) { return APP_INVALID; }
    if (memcmp(snapshot.track_id,a->model.player.track_id,16)) {
        if (memcmp(snapshot.track_id,a->model.playing.id,16)) { a->model.playing = (AppEntity){0}; }
        a->widgets.seeks[0] = (UiSeekSlider){0};
    }
    PlayerSnapshot before=a->model.player;
    a->model.player = snapshot;
    if (!a->widgets.seeks[0].dragging) {
        a->widgets.seeks[0].value = (snapshot.timeline_valid || snapshot.state == PLAYER_PAUSED) && snapshot.duration_frames ?
            fmin(1,(double)snapshot.position_frames / (double)snapshot.duration_frames) : 0;
    }
    if (a->scheduler) { ui_scheduler_playing(a->scheduler,snapshot.state == PLAYER_PLAYING,now); }
    if (before.state!=snapshot.state || before.generation!=snapshot.generation || before.volume!=snapshot.volume ||
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
AppResult app_session(App *a, bool logged_in, bool online, uint64_t now)
{
    if (!a || !a->alive) { return APP_INVALID; }
    a->model.auth_pending = false;
    bool changed = a->model.logged_in != logged_in || a->model.online != online ||
        (logged_in && a->model.status == APP_AUTH_EXPIRED);
    a->model.logged_in = logged_in; a->model.online = online;
    if (!changed) { dirty(a); return APP_OK; }
    AppResult r = generation(a);
    if (r != APP_OK) { return r; }
    if (!logged_in) {
        a->model.history_count = 0; memset(a->model.history,0,sizeof(a->model.history));
        a->model.selected = a->model.playing = (AppEntity){0};
        a->model.location = (AppLocation){.screen=APP_HOME};
        a->model.player = (PlayerSnapshot){0};
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
    if (!a || !a->alive || action.type < APP_GO_HOME || action.type > APP_DETAIL ||
        (action.type==APP_SELECT && (!memchr(action.entity.uri,0,sizeof(action.entity.uri)) ||
        !memchr(action.entity.title,0,sizeof(action.entity.title))))) { return APP_INVALID; }
    if (a->action_count == APP_ACTIONS) { a->model.last_result = APP_FULL; dirty(a); return APP_FULL; }
    a->actions[a->action_count++] = action; dirty(a); return APP_OK;
}
static AppResult apply(App *a, AppAction action, uint64_t now)
{
    AppModel *m = &a->model;
    switch (action.type) {
    case APP_GO_HOME: return navigate(a,(AppLocation){.screen=APP_HOME},false);
    case APP_GO_LIBRARY: return navigate(a,(AppLocation){.screen=APP_LIBRARY},false);
    case APP_GO_SETTINGS: return navigate(a,(AppLocation){.screen=APP_SETTINGS},false);
    case APP_BACK:
        if (!m->history_count) { return APP_CANCELLED; }
        return navigate(a,m->history[--m->history_count],true);
    case APP_RETRY:
        if (m->pending) { return APP_PENDING; }
        if (generation(a) != APP_OK) { return APP_FULL; } return request(a,0);
    case APP_MORE:
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
        if (action.entity.kind == COLLECTION_SHELF) { return navigate(a,(AppLocation){.screen=APP_SHELF,.entity=action.entity},false); }
        if (action.entity.playable) {
            PlayerCommand c = {.type=PLAYER_LOAD,.play=true}; memcpy(c.track_id,action.entity.id,16);
            AppResult r = command(a,c); if (r == APP_OK) { m->playing = action.entity; } return r;
        }
        m->detail = true; return APP_OK;
    case APP_TOGGLE_PLAY: return command(a,(PlayerCommand){.type=m->player.state == PLAYER_PLAYING ? PLAYER_PAUSE : PLAYER_PLAY});
    case APP_PREVIOUS: return command(a,(PlayerCommand){.type=PLAYER_PREVIOUS});
    case APP_NEXT: return command(a,(PlayerCommand){.type=PLAYER_NEXT});
    case APP_SEEK:
        if (!isfinite(action.value) || !m->player.duration_frames ||
            (!m->player.timeline_valid && m->player.state != PLAYER_PAUSED)) { return APP_INVALID; }
        if (action.value >= 1) { return command(a,(PlayerCommand){.type=PLAYER_SEEK,.position_frames=m->player.duration_frames}); }
        return command(a,(PlayerCommand){.type=PLAYER_SEEK,.position_frames=(uint64_t)
            (fmax(0,action.value) * (double)m->player.duration_frames)});
    case APP_VOLUME:
        if (!isfinite(action.value)) { return APP_INVALID; }
        return command(a,(PlayerCommand){.type=PLAYER_VOLUME,.volume=(float)fmin(1,fmax(0,action.value))});
    case APP_DETAIL: m->detail = !m->detail; return APP_OK;
    default: return APP_INVALID;
    }
}
AppResult app_update(App *a, uint64_t now)
{
    if (!a || !a->alive || a->widgets.in_frame) { return APP_INVALID; }
    uint32_t count = a->action_count; a->action_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        AppAction action=a->actions[i]; a->model.last_result = apply(a,action,now);
        if (action.type==APP_SIGN_OUT) { break; } /* Discard pre-logout UI intents. */
    }
    memset(a->actions,0,sizeof(a->actions));
    if (a->model.logged_in) {
        if (a->services.player) { (void)app_publish_player(a,player_snapshot(a->services.player),now); }
        else if (a->services.snapshot) { (void)app_publish_player(a,a->services.snapshot(a->services.context),now); }
    }
    if (a->services.artwork) {
        (void)artwork_begin_visible(a->services.artwork);
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
