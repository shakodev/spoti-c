#include "ui/app.h"
#include <math.h>
#include <string.h>

void app_lyrics_hold(App *a)
{
    if (a->model.location.screen!=APP_LYRICS) { return; }
    a->catalog.hold=core_deadline_after(a->catalog.now,APP_LYRICS_HOLD);
    if (a->scheduler) { ui_scheduler_work(a->scheduler,a->catalog.hold); }
}
static void request_artwork(App *a, const AppEntity *entity, JsonString image, uint32_t width, uint32_t height, uint16_t size)
{
    if (!image.bytes || image.bytes>=ART_URL_CHARS || a->image_count==APP_IMAGES) { return; }
    for (uint32_t n=0;n<a->image_count;++n) { if (!strcmp(a->images[n].entity.uri,entity->uri)) { return; } }
    wchar_t url[ART_URL_CHARS];
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,image.data,(int)image.bytes,url,ART_URL_CHARS-1);
    if (!n) { return; } url[n]=0;
    ArtworkId id={.size=size}; memcpy(id.content,entity->id,16);
    ArtworkSource source={url,width,height};
    (void)app_image(a,a->model.generation,entity,id,&source,1);
}
static void artwork(App *a)
{
    const CatalogPage *page=a->catalog.page;
    if (!page || !a->services.artwork) { return; }
    a->image_count=0;
    if (a->catalog.artwork[0] && (a->model.location.screen==APP_ALBUM || a->model.location.screen==APP_ARTIST)) {
        request_artwork(a,&a->model.location.entity,(JsonString){a->catalog.artwork,strlen(a->catalog.artwork)},
            a->catalog.artwork_width,a->catalog.artwork_height,160);
    }
    UiListRange range;
    if (ui_virtual_range(page->count,APP_CATALOG_ROW,a->widgets.lists[0].scroll,
        app_catalog_viewport(a),0,&range)!=UI_WIDGET_OK) { return; }
    for (uint32_t i=range.first;i<range.end && i<range.first+APP_IMAGES;++i) {
        const CatalogItem *item=&page->items[i]; AppEntity entity;
        if (!item->image.url.bytes || app_catalog_entity(item,&entity)!=APP_OK) { continue; }
        request_artwork(a,&entity,item->image.url,item->image.width,item->image.height,40);
    }
}
void app_catalog_prepare(App *a, uint64_t now)
{
    a->catalog.now=now;
    if (a->model.location.screen==APP_SEARCH && core_deadline_expired(now,a->catalog.debounce)) {
        a->catalog.debounce=CORE_CLOCK_NEVER;
        a->model.last_result=app_catalog_fetch(a,0);
    }
    if (a->model.location.screen==APP_LYRICS) {
        if (memcmp(a->catalog.track_id,a->model.player.track_id,16)) { (void)app_refresh(a); }
        bool visible=!a->scheduler || (a->scheduler->visible && !a->scheduler->occluded);
        LyricsView before=a->catalog.lyric;
        a->catalog.lyric=lyrics_timeline_update(&a->catalog.timeline,&a->model.player,visible,now);
        if (core_deadline_expired(now,a->catalog.hold)) { a->catalog.hold=CORE_CLOCK_NEVER; }
        if (a->catalog.hold==CORE_CLOCK_NEVER && a->catalog.lyric.line!=LYRICS_NO_LINE) {
            a->widgets.lists[0].scroll=fmax(0,(double)a->catalog.lyric.line*48-96);
        }
        if (a->scheduler) {
            ui_scheduler_lyrics(a->scheduler,visible,a->catalog.lyric.deadline_ms);
            if (before.line!=a->catalog.lyric.line || before.valid!=a->catalog.lyric.valid) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_LYRICS); }
        }
    }
    if (a->scheduler) { ui_scheduler_work(a->scheduler,a->catalog.debounce<a->catalog.hold ? a->catalog.debounce : a->catalog.hold); }
    artwork(a);
}
static uint64_t line_key(void *context, uint32_t index) { (void)context; return (uint64_t)index+1; }
static void line(void *context, uint32_t index, uint64_t key)
{
    (void)key; App *a=context; const LyricsDocument *doc=a->catalog.timeline.document;
    JsonString text=doc->lines[index].text;
    /* Retained immutable text may exceed the small button-label cache. The font
     * service clips its bounded run; don't turn a long lyric into a UI error. */
    size_t n=text.bytes<191 ? text.bytes : 191;
    while (n<text.bytes && n && ((unsigned char)text.data[n]&0xc0)==0x80) { --n; }
    Clay_Color color=a->catalog.lyric.valid && index==a->catalog.lyric.line ? (Clay_Color){110,235,155,255} : (Clay_Color){185,185,185,255};
    CLAY(CLAY_IDI("LyricLine",index),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(48)},.padding={4,4,8,8}},
        .clip={.horizontal=true,.vertical=true}}) { app_label(app_text(a,(Clay_String){false,(int32_t)n,text.data}),22,color); }
}
void app_lyrics(App *a)
{
    const LyricsDocument *doc=a->catalog.timeline.document;
    app_catalog_status(a);
    if (!doc) {
        if (a->model.status==APP_EMPTY) { app_message(a,CLAY_STRING("Lyrics unavailable for this track"),APP_INFORMATION); }
        return;
    }
    const char *s=doc->availability==LYRICS_MISSING || doc->availability==LYRICS_EMPTY ? "Lyrics unavailable" :
        doc->availability==LYRICS_CAPPED ? "Lyrics restricted / preview only" : doc->sync==LYRICS_PLAIN ? "Unsynchronized lyrics" : "Synchronized lyrics";
    app_message(a,(Clay_String){true,(int32_t)strlen(s),s},doc->availability==LYRICS_CAPPED ? APP_WARNING : APP_INFORMATION);
    (void)app_button(a,CLAY_ID("LyricsFollow"),a->catalog.hold==CORE_CLOCK_NEVER ? CLAY_STRING("Following") : CLAY_STRING("Resume follow"),120,
        (AppAction){.type=APP_LYRICS_FOLLOW},a->catalog.hold==CORE_CLOCK_NEVER);
    UiListRange range; double scroll=a->widgets.lists[0].scroll;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("LyricsList"),&a->widgets.lists[0],doc->count+doc->preview_count,
        48,fminf(384,fmaxf(48,a->geometry.viewport-72)),0,line_key,line,a,&range);
    if (scroll!=a->widgets.lists[0].scroll && a->widgets.input.down) { app_lyrics_hold(a); }
    if (a->control_count<APP_CONTROLS) { a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("LyricsList"),.action={.type=APP_LYRICS_FOLLOW}}; }
}
