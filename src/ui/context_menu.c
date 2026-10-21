#include "app.h"
#include "screens/playlist.h"
#include "protocol/media_source.h"
#include <math.h>
#include <string.h>

void app_context_close(App *a)
{
    if (a->menu.open) { a->widgets.focus=a->menu.return_focus; }
    a->menu.open=false; a->widgets.active=0;
}
static void add(AppMenu *menu, const char *label, AppAction action, bool disabled)
{
    if (menu->count==3) { return; }
    uint32_t at=menu->count++; menu->actions[at]=action; menu->labels[at]=label; menu->disabled[at]=disabled;
    if (menu->selected==UINT32_MAX && !disabled) { menu->selected=at; }
}
AppResult app_context_open(App *a, float x, float y, bool keyboard)
{
    if (!a || !a->alive || a->widgets.in_frame || !isfinite(x) || !isfinite(y)) { return APP_INVALID; }
    app_context_close(a);
    AppControl *target=NULL;
    for (uint32_t n=0;n<a->control_count;++n) {
        AppControl *c=&a->controls[n];
        if (c->action.type!=APP_CATALOG_SELECT && c->action.type!=APP_TRACK_PLAY) { continue; }
        Clay_ElementData bounds=Clay_GetElementData(c->id); Clay_BoundingBox b=bounds.boundingBox;
        if ((keyboard && a->widgets.focus==c->id.id) || (!keyboard && bounds.found && x>=b.x && x<b.x+b.width && y>=b.y && y<b.y+b.height)) {
            target=c; if (keyboard) { x=b.x; y=b.y; } break;
        }
    }
    if (!target) { return APP_CANCELLED; }
    AppControl resolved=*target;
    if (!keyboard && (target->id.id==CLAY_ID("CatalogList").id || target->id.id==CLAY_ID("PlaylistList").id)) {
        Clay_ElementData bounds=Clay_GetElementData(target->id); UiListRange range; uint32_t hit;
        uint32_t count=target->action.type==APP_CATALOG_SELECT ? a->catalog.page->count : a->services.playlist->count;
        double height=target->action.type==APP_CATALOG_SELECT ? APP_CATALOG_ROW : UI_TRACK_HEIGHT;
        double viewport=target->action.type==APP_CATALOG_SELECT ? app_catalog_viewport(a) : app_playlist_viewport(a);
        if (ui_virtual_range(count,height,a->widgets.lists[0].scroll,viewport,0,&range)!=UI_WIDGET_OK ||
            !ui_virtual_hit(&range,y-bounds.boundingBox.y,&hit)) { return APP_CANCELLED; }
        resolved.action.value=target->action.type==APP_CATALOG_SELECT ? hit : (double)playlist_key(a->services.playlist,hit);
        target=&resolved;
    }
    AppMenu *menu=&a->menu;
    *menu=(AppMenu){.generation=a->model.generation,.return_focus=a->widgets.focus,.selected=UINT32_MAX,
        .x=fmaxf(8,fminf(x,a->geometry.width-264)),.y=fmaxf(8,fminf(y,a->geometry.height-a->geometry.bar-144))};
    bool unavailable=target->disabled || !a->model.logged_in || !a->model.online || a->model.pending;
    if (target->action.type==APP_CATALOG_SELECT && a->catalog.page) {
        uint32_t index;
        if (target->action.value>=0) { index=(uint32_t)target->action.value; }
        else if (!app_catalog_index(a,a->widgets.lists[0].focus,&index)) { return APP_CANCELLED; }
        if (index>=a->catalog.page->count) { return APP_STALE; }
        const CatalogItem *item=&a->catalog.page->items[index];
        AppEntity entity; bool valid=app_catalog_entity(item,&entity)==APP_OK;
        add(menu,item->kind==CATALOG_TRACK ? "Play" : "Open",(AppAction){.type=APP_CATALOG_SELECT,.value=index},unavailable || !valid);
        if (item->kind==CATALOG_TRACK && item->as.track.album_uri.bytes) {
            AppEntity album={.kind=COLLECTION_ALBUM};
            if (json_base_string_copy(item->as.track.album_uri,album.uri,sizeof(album.uri))==JSON_OK &&
                !strncmp(album.uri,"spotify:album:",14) && media_gid_from_base62(album.uri+14,album.id)==MEDIA_OK) {
                strcpy_s(album.title,sizeof(album.title),"Album");
                add(menu,"Go to album",(AppAction){.type=APP_SELECT,.entity=album},!a->model.logged_in || a->model.pending);
            }
        }
    } else if (target->action.type==APP_TRACK_PLAY && a->services.playlist) {
        uint64_t key=target->action.value>=1 ? (uint64_t)target->action.value : a->widgets.lists[0].focus;
        PlaylistQueue queue;
        add(menu,"Play from this track",(AppAction){.type=APP_TRACK_PLAY,.value=(double)key},
            unavailable || playlist_queue(a->services.playlist,key,&queue)!=TRACK_OK);
    }
    if (!menu->count) { return APP_CANCELLED; }
    if (menu->disabled[0]) { strcpy_s(menu->reason,sizeof(menu->reason),!a->model.online ?
        "You are offline." : a->model.pending ? "Wait for this page to finish loading." : "Playback is unavailable for this item."); }
    menu->open=true; a->widgets.active=0;
    if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_INPUT); }
    return APP_OK;
}
AppResult app_context_key(App *a, AppKey key)
{
    AppMenu *m=&a->menu;
    if (!m->open) { return APP_CANCELLED; }
    if (m->generation!=a->model.generation || key==APP_KEY_ESCAPE || key==APP_KEY_TAB || key==APP_KEY_BACK) {
        app_context_close(a); return APP_OK;
    }
    if (key==APP_KEY_ENTER) {
        if (m->selected>=m->count || m->disabled[m->selected]) { return APP_CANCELLED; }
        AppAction action=m->actions[m->selected]; app_context_close(a); return app_enqueue(a,action);
    }
    if (key!=APP_KEY_UP && key!=APP_KEY_DOWN && key!=APP_KEY_HOME && key!=APP_KEY_END) { return APP_CANCELLED; }
    uint32_t selected=key==APP_KEY_HOME ? m->count-1 : key==APP_KEY_END ? 0 : m->selected;
    bool reverse=key==APP_KEY_UP || key==APP_KEY_END;
    for (uint32_t n=0;n<m->count;++n) {
        selected=selected>=m->count ? 0 : reverse ? (selected+m->count-1)%m->count : (selected+1)%m->count;
        if (!m->disabled[selected]) { m->selected=selected; break; }
    }
    return APP_OK;
}
void app_context_layout(App *a)
{
    AppMenu *m=&a->menu;
    if (!m->open) { return; }
    if (m->generation!=a->model.generation || a->widgets.input.cancel ||
        (a->widgets.input.pressed && !Clay_PointerOver(CLAY_ID("RouteMenu")))) { app_context_close(a); return; }
    CLAY(CLAY_ID("RouteMenu"),{.layout={.sizing={CLAY_SIZING_FIXED(256),CLAY_SIZING_FIT()},.padding=CLAY_PADDING_ALL(4),
        .layoutDirection=CLAY_TOP_TO_BOTTOM,.childGap=4},.backgroundColor=UI_HIGHLIGHT,.cornerRadius=CLAY_CORNER_RADIUS(4),
        .floating={.attachTo=CLAY_ATTACH_TO_ROOT,.offset={m->x,m->y},.zIndex=100}}) {
        for (uint32_t n=0;n<m->count;++n) {
            Clay_ElementId id=CLAY_IDI("RouteMenuItem",n);
            if (n==m->selected) { a->widgets.focus=id.id; }
            if (ui_component_button(&a->widgets,id,(Clay_String){true,(int32_t)strlen(m->labels[n]),m->labels[n]},248,36,m->disabled[n],
                (UiButtonStyle){.kind=UI_BUTTON_MENU,.selected=n==m->selected})) {
                AppAction action=m->actions[n]; app_context_close(a); (void)app_enqueue(a,action);
            }
            if (m->open && a->widgets.hover==id.id && !m->disabled[n]) { m->selected=n; }
        }
        if (m->reason[0]) {
            CLAY_TEXT(((Clay_String){false,(int32_t)strlen(m->reason),m->reason}),CLAY_TEXT_CONFIG({.fontSize=13,.textColor=UI_SUBDUED,.wrapMode=CLAY_TEXT_WRAP_WORDS}));
        }
    }
}
