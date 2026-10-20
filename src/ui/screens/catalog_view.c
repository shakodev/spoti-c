#include "ui/app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void changed(App *a)
{
    if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_STATE); }
    if (a->services.wake) { a->services.wake(a->services.context); }
}
static JsonString span(const char *s) { return (JsonString){s,strlen(s)}; }
/* Long display names truncate at a scalar boundary; identities never truncate. */
static void title(JsonString s, char out[192])
{
    size_t n=s.bytes<191 ? s.bytes : 191;
    while (n<s.bytes && n && ((unsigned char)s.data[n]&0xc0)==0x80) { --n; }
    if (n) { memcpy(out,s.data,n); } out[n]=0;
}
bool app_is_catalog(const App *a) { return a->model.location.screen>=APP_SEARCH; }
void app_catalog_clear(App *a)
{
    if (a->catalog.page) { (void)page_release((JsonPage *)&a->catalog.page->json); }
    (void)lyrics_timeline_set(&a->catalog.timeline,NULL);
    uint64_t now=a->catalog.now;
    a->catalog=(CatalogView){.debounce=CORE_CLOCK_NEVER,.hold=CORE_CLOCK_NEVER,.now=now,
        .lyric={.line=LYRICS_NO_LINE,.deadline_ms=CORE_CLOCK_NEVER}};
    if (a->scheduler) { ui_scheduler_lyrics(a->scheduler,false,CORE_CLOCK_NEVER); ui_scheduler_work(a->scheduler,CORE_CLOCK_NEVER); }
}
AppResult app_catalog_entity(const CatalogItem *item, AppEntity *out)
{
    if (!item || !out) { return APP_INVALID; }
    *out=(AppEntity){.kind=item->kind==CATALOG_TRACK ? COLLECTION_TRACK : item->kind==CATALOG_ALBUM ?
        COLLECTION_ALBUM : item->kind==CATALOG_ARTIST ? COLLECTION_ARTIST : COLLECTION_UNKNOWN};
    title(item->name,out->title); if (!out->title[0]) { strcpy_s(out->title,sizeof(out->title),"Unavailable item"); }
    if (!item->uri.bytes || json_base_string_copy(item->uri,out->uri,sizeof(out->uri))!=JSON_OK) { return APP_UNAVAILABLE; }
    const CatalogId *id=item->kind==CATALOG_TRACK ? &item->as.track.playable_id : &item->id;
    memcpy(out->id,id->bytes,16);
    out->playable=item->kind==CATALOG_TRACK && item->available && id->valid;
    return item->id.valid && item->available && (item->kind!=CATALOG_TRACK || out->playable) ? APP_OK : APP_UNAVAILABLE;
}
AppResult app_catalog_fetch(App *a, uint32_t cursor)
{
    AppModel *m=&a->model; AppLocation *l=&m->location;
    if (!m->logged_in || !m->online) { return APP_UNAVAILABLE; }
    if (m->pending) { return APP_PENDING; }
    if (m->serial==UINT64_MAX) { return APP_FULL; }
    m->request=(AppRequest){.generation=m->generation,.serial=++m->serial,.screen=l->screen,
        .cursor=cursor,.selector=l->selector};
    memcpy(m->request.uri,l->screen==APP_LYRICS ? m->playing.uri : l->entity.uri,sizeof(m->request.uri));
    memcpy(m->request.query,l->query,sizeof(m->request.query));
    AppResult r=a->services.catalog ? a->services.catalog(a->services.context,&m->request,span(m->playing_artwork)) : APP_UNAVAILABLE;
    m->pending=r==APP_PENDING;
    if (!a->catalog.page && !a->catalog.timeline.document) { m->status=m->pending ? APP_LOADING : APP_ERROR; }
    return r;
}
AppResult app_catalog_request(App *a, uint32_t cursor)
{
    if (a->model.location.screen==APP_SEARCH && !cursor) {
        if (!a->model.location.query[0]) { a->model.status=APP_EMPTY; return APP_OK; }
        a->catalog.debounce=core_deadline_after(a->catalog.now,APP_SEARCH_DELAY);
        if (a->scheduler) { ui_scheduler_work(a->scheduler,a->catalog.debounce); }
        a->model.status=APP_LOADING; return APP_PENDING;
    }
    if (a->model.location.screen==APP_LYRICS) {
        memcpy(a->catalog.track_id,a->model.player.track_id,16);
        if (!a->model.playing.uri[0] || !a->model.playing_artwork[0] ||
            memcmp(a->model.playing.id,a->model.playing_artwork_id,16)) { a->model.status=APP_EMPTY; return APP_UNAVAILABLE; }
    }
    return app_catalog_fetch(a,cursor);
}
static bool accepts(App *a, AppRequest r, AppScreen screen)
{
    return a && a->alive && a->model.pending && a->model.location.screen==screen &&
        r.generation==a->model.generation && r.serial==a->model.request.serial &&
        r.screen==screen && r.cursor==a->model.request.cursor && r.selector==a->model.request.selector &&
        memchr(r.uri,0,sizeof(r.uri)) && memchr(r.query,0,sizeof(r.query)) &&
        !strcmp(r.uri,a->model.request.uri) && !strcmp(r.query,a->model.request.query);
}
AppResult app_catalog_deliver(App *a, AppRequest r, CatalogResult result, const CatalogPage *page)
{
    AppResult outcome=APP_STALE;
    if (r.screen>=APP_SEARCH && r.screen<=APP_ARTIST && accepts(a,r,r.screen)) {
        a->model.pending=false; a->catalog.result=result; outcome=APP_FAILED;
        ApiOperation operation=r.screen==APP_ARTIST ? API_ARTIST : r.screen==APP_ALBUM ?
            r.cursor ? API_ALBUM_TRACKS : API_ALBUM : r.selector==SEARCH_TRACKS ? API_SEARCH_TRACKS :
            r.selector==SEARCH_LIST ? API_SEARCH_LIST : API_SEARCH;
        JsonString key=span(r.screen==APP_SEARCH ? r.query : r.uri);
        if (page && (result==CATALOG_OK || result==CATALOG_PARTIAL || result==CATALOG_BOUNDARY) &&
            page->operation==operation && json_span_equal(page->requested_key,key) &&
            page->json.generation==r.generation && page->offset==r.cursor && page->count<=CATALOG_PAGE_LIMIT &&
            (page->next==CATALOG_END || page->next>r.cursor)) {
            if (a->catalog.page) { (void)page_release((JsonPage *)&a->catalog.page->json); }
            a->catalog.page=page; page=NULL; outcome=APP_OK;
            const CatalogItem *detail=&a->catalog.page->detail;
            if (detail->name.bytes) {
                title(detail->name,a->catalog.title);
                if (detail->kind==CATALOG_ALBUM) { title(detail->as.album.artist_name,a->catalog.subtitle); }
                else if (detail->kind==CATALOG_ARTIST && detail->as.artist.followers) {
                    (void)snprintf(a->catalog.subtitle,sizeof(a->catalog.subtitle),"%llu followers",
                        (unsigned long long)detail->as.artist.followers);
                } else { a->catalog.subtitle[0]=0; }
                a->catalog.artwork[0]=0;
                (void)json_base_string_copy(detail->image.url,a->catalog.artwork,sizeof(a->catalog.artwork));
                a->catalog.artwork_width=detail->image.width; a->catalog.artwork_height=detail->image.height;
            }
            a->model.status=a->catalog.page->count ? APP_CONTENT : APP_EMPTY;
            a->widgets.lists[0].scroll=r.cursor ? 0 : a->model.location.scroll;
            a->widgets.lists[0].selection=0;
            a->widgets.lists[0].focus=a->catalog.page->count ? app_catalog_key(a,0) : 0;
            a->image_count=0;
        } else {
            a->model.status=result==CATALOG_AUTH ? APP_AUTH_EXPIRED : APP_ERROR;
            if (result==CATALOG_AUTH && a->catalog.page) {
                (void)page_release((JsonPage *)&a->catalog.page->json); a->catalog.page=NULL; a->image_count=0;
            }
        }
        a->model.last_result=outcome;
    }
    if (page) { (void)page_release((JsonPage *)&page->json); }
    if (a && a->alive) { if (outcome==APP_STALE) { ++a->stale_deliveries; } changed(a); }
    return outcome;
}
AppResult app_lyrics_deliver(App *a, AppRequest r, LyricsResult result, const LyricsDocument *doc)
{
    AppResult outcome=APP_STALE;
    if (accepts(a,r,APP_LYRICS)) {
        a->model.pending=false; a->catalog.lyrics_result=result; outcome=APP_FAILED;
        if (result==LYRICS_OK && doc && doc->generation==r.generation &&
            !memcmp(doc->track_id,a->catalog.track_id,16) && lyrics_timeline_set(&a->catalog.timeline,doc)) {
            a->model.status=APP_CONTENT; outcome=APP_OK;
        } else { a->model.status=result==LYRICS_AUTH ? APP_AUTH_EXPIRED : APP_ERROR; }
        a->model.last_result=outcome;
    }
    if (doc) { (void)lyrics_document_release(doc); }
    if (a && a->alive) { if (outcome==APP_STALE) { ++a->stale_deliveries; } changed(a); }
    return outcome;
}
uint64_t app_catalog_key(void *context, uint32_t index)
{
    App *a=context; const CatalogItem *item=&a->catalog.page->items[index];
    /* Stable source identity + occurrence distinguishes duplicates without
     * conflating identical names. Full compact IDs remain in the play command. */
    uint64_t hash=1469598103934665603ull;
    for (unsigned i=0;i<16;++i) { hash=(hash^item->id.bytes[i])*1099511628211ull; }
    return (hash<<32)|((uint64_t)a->catalog.page->offset+index+1);
}
bool app_catalog_index(void *context, uint64_t key, uint32_t *out)
{
    App *a=context; if (!a->catalog.page) { return false; }
    for (uint32_t i=0;i<a->catalog.page->count;++i) { if (app_catalog_key(a,i)==key) { *out=i; return true; } }
    return false;
}
AppResult app_catalog_activate(App *a, uint32_t index)
{
    if (!a->catalog.page || index>=a->catalog.page->count || a->model.pending) { return APP_INVALID; }
    const CatalogItem *item=&a->catalog.page->items[index]; AppEntity entity;
    AppResult r=app_catalog_entity(item,&entity); if (r!=APP_OK) { return r; }
    if (entity.playable) {
        memcpy(a->model.playing_artwork_id,entity.id,16);
        title(item->as.track.artist_name,a->model.playing_artist);
        memcpy(a->model.playing_artist_id,entity.id,16);
        JsonString image=item->image.url;
        if (!image.bytes && a->catalog.page->detail.kind==CATALOG_ALBUM) { image=a->catalog.page->detail.image.url; }
        if (json_base_string_copy(image,a->model.playing_artwork,sizeof(a->model.playing_artwork))!=JSON_OK) { a->model.playing_artwork[0]=0; }
    }
    return app_enqueue(a,(AppAction){.type=APP_SELECT,.entity=entity});
}
uint32_t app_catalog_play_index(const App *a)
{
    if (!a || !a->model.logged_in || !a->model.online || a->model.pending || !a->catalog.page) { return CATALOG_END; }
    for (uint32_t i=0;i<a->catalog.page->count;++i) {
        const CatalogItem *item=&a->catalog.page->items[i]; AppEntity entity;
        if (item->kind==CATALOG_TRACK && app_catalog_entity(item,&entity)==APP_OK && entity.playable) { return i; }
    }
    return CATALOG_END;
}
static void row(void *context, uint32_t index, uint64_t key)
{
    App *a=context; const CatalogItem *item=&a->catalog.page->items[index];
    AppEntity entity; bool enabled=app_catalog_entity(item,&entity)==APP_OK;
    uint32_t slot=a->catalog.rendered++;
    char subtitle[192], duration[32]; title(item->kind==CATALOG_TRACK ? item->as.track.artist_name :
        item->kind==CATALOG_ALBUM ? item->as.album.artist_name : item->restriction,subtitle);
    if (!subtitle[0]) { strcpy_s(subtitle,sizeof(subtitle),item->kind==CATALOG_ARTIST ? "Artist" : item->kind==CATALOG_ALBUM ? "Album" : "Track"); }
    if (!enabled) { strcpy_s(subtitle,sizeof(subtitle),item->kind==CATALOG_ITEM_UNSUPPORTED ? "Unsupported item" : "Unavailable / restricted"); }
    uint32_t ms=item->kind==CATALOG_TRACK ? item->as.track.duration_ms : 0;
    (void)snprintf(duration,sizeof(duration),"%u:%02u",ms/60000,ms/1000%60);
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(APP_CATALOG_ROW)},.childGap=8,.padding={4,4,4,4}}}) {
        float width=a->geometry.main-(a->geometry.compact ? 88 : 144);
        AppAction action={.type=APP_CATALOG_SELECT,.value=index};
        AppImage *image=NULL;
        for (uint32_t n=0;n<a->image_count;++n) { if (!strcmp(a->images[n].entity.uri,entity.uri)) { image=&a->images[n]; break; } }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(32),CLAY_SIZING_FIXED(32)}},
            .backgroundColor={40,48,44,255},.cornerRadius=CLAY_CORNER_RADIUS(4),.custom={.customData=image}}) { }
        CLAY(CLAY_IDI("CatalogRowClip",slot),{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(44)},.layoutDirection=CLAY_TOP_TO_BOTTOM},
            .clip={.horizontal=true,.vertical=true}}) {
            (void)app_track_control(a,CLAY_IDI("CatalogItem",(uint32_t)(key^(key>>32))),
                (Clay_String){false,(int32_t)strlen(entity.title),entity.title},
                width,action,!enabled || a->model.pending,item->kind==CATALOG_TRACK && entity.playable &&
                    a->model.player.state!=PLAYER_STOPPED && a->model.player.state!=PLAYER_ERROR &&
                    !memcmp(entity.id,a->model.player.track_id,16));
            if (subtitle[0]) { app_label(app_text(a,(Clay_String){false,(int32_t)strlen(subtitle),subtitle}),12,(Clay_Color){180,180,180,255}); }
        }
        if (item->kind==CATALOG_TRACK && !a->geometry.compact) {
            app_label(app_text(a,(Clay_String){false,(int32_t)strlen(duration),duration}),14,(Clay_Color){170,170,170,255});
        }
    }
}
void app_catalog_rows(App *a)
{
    const CatalogPage *p=a->catalog.page; if (!p) { return; }
    UiListRange range; a->catalog.rendered=0;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("CatalogList"),&a->widgets.lists[0],p->count,
        APP_CATALOG_ROW,app_catalog_viewport(a),0,app_catalog_key,row,a,&range);
    if (a->control_count<APP_CONTROLS) { a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("CatalogList"),
        .action={.type=APP_CATALOG_SELECT,.value=-1},.disabled=a->model.pending}; }
    if (p->partial || p->truncated || p->boundary) {
        app_message(a,CLAY_STRING("Partial results / service boundary"),APP_WARNING);
    }
    if (p->next!=CATALOG_END) {
        (void)app_button(a,CLAY_ID("CatalogMore"),CLAY_STRING("Next page"),100,(AppAction){.type=APP_MORE},a->model.pending);
    }
}
float app_catalog_viewport(const App *a)
{
    float header=a->model.location.screen==APP_ALBUM || a->model.location.screen==APP_ARTIST ?
        (a->geometry.compact ? 116.0f : 172.0f) : 0;
    return fminf(336,fmaxf(48,a->geometry.viewport-120-header));
}
void app_catalog_header(App *a)
{
    const char *s=a->catalog.title[0] ? a->catalog.title : a->model.location.entity.title;
    AppImage *image=NULL;
    for (uint32_t i=0;i<a->image_count;++i) {
        if (!strcmp(a->images[i].entity.uri,a->model.location.entity.uri)) { image=&a->images[i]; break; }
    }
    float cover=a->geometry.compact ? 72.0f : 128.0f;
    CLAY(CLAY_ID("CatalogHero"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(cover)},
        .childGap=16,.childAlignment={.y=CLAY_ALIGN_Y_BOTTOM}},.clip={.horizontal=true,.vertical=true}}) {
        CLAY(CLAY_ID("CatalogCover"),{.layout={.sizing={CLAY_SIZING_FIXED(cover),CLAY_SIZING_FIXED(cover)}},
            .backgroundColor={40,40,40,255},.cornerRadius=CLAY_CORNER_RADIUS(a->model.location.screen==APP_ARTIST ? cover/2 : 4.0f),
            .custom={.customData=image}}) { }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(cover)},.childGap=8,
            .layoutDirection=CLAY_TOP_TO_BOTTOM,.childAlignment={.y=CLAY_ALIGN_Y_BOTTOM}},.clip={.horizontal=true,.vertical=true}}) {
            app_label(a->model.location.screen==APP_ARTIST ? CLAY_STRING("Artist") : CLAY_STRING("Album"),12,(Clay_Color){220,220,220,255});
            app_label(app_text(a,(Clay_String){false,(int32_t)strlen(s),s}),a->geometry.compact ? 24 : 32,(Clay_Color){255,255,255,255});
            app_label((Clay_String){false,(int32_t)strlen(a->catalog.subtitle),a->catalog.subtitle},14,(Clay_Color){190,190,190,255});
        }
    }
    uint32_t index=app_catalog_play_index(a);
    (void)app_button(a,CLAY_ID("CatalogPlay"),CLAY_STRING("Play first track"),128,
        (AppAction){.type=APP_CATALOG_SELECT,.value=index==CATALOG_END ? -1 : (double)index},index==CATALOG_END);
    if (index==CATALOG_END && a->model.status==APP_CONTENT) {
        app_label(CLAY_STRING("No playable tracks on this page"),12,(Clay_Color){180,180,180,255});
    }
}
void app_catalog_status(App *a)
{
    AppStatus status=a->model.status;
    const char *s=status==APP_LOADING ? "Loading..." : status==APP_EMPTY ? "No results" :
        status==APP_SIGNED_OUT ? "Sign in to browse music" : status==APP_AUTH_EXPIRED ? "Session expired. Sign in again" :
        status==APP_OFFLINE ? "You are offline" : status==APP_ERROR ?
        a->catalog.result==CATALOG_UNSUPPORTED ? "This route is not supported by the current service" :
        a->catalog.result==CATALOG_RESTRICTED ? "Unavailable for this account" :
        a->catalog.result==CATALOG_NOT_FOUND ? "This content is no longer available" :
        a->catalog.result==CATALOG_RATE_LIMIT ? "Service is busy. Try again later" :
        "Unable to load. Retry this page" : NULL;
    if (s) { app_message(a,(Clay_String){true,(int32_t)strlen(s),s},
        status==APP_ERROR || status==APP_AUTH_EXPIRED ? APP_FAILURE : status==APP_OFFLINE ? APP_WARNING : APP_INFORMATION); }
    if (status==APP_ERROR || status==APP_OFFLINE) {
        (void)app_button(a,CLAY_ID("CatalogRetry"),CLAY_STRING("Retry"),64,(AppAction){.type=APP_RETRY},a->model.pending);
    }
    if (a->catalog.result==CATALOG_RESTRICTED) { app_message(a,CLAY_STRING("Unavailable for this account"),APP_WARNING); }
}
