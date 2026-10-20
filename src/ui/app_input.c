#include "app.h"
#include "screens/playlist.h"
#include <math.h>

AppResult app_pointer(App *a, UiWidgetInput input)
{
    if (!a || !a->alive || !isfinite(input.x) || !isfinite(input.y)) { return APP_INVALID; }
    if (a->input_count == APP_INPUTS) {
        if (!input.pressed && !input.released && !input.cancel && !input.activate &&
            !a->inputs[APP_INPUTS-1].pressed && !a->inputs[APP_INPUTS-1].released && !a->inputs[APP_INPUTS-1].cancel) {
            a->inputs[APP_INPUTS-1] = input; return APP_OK;
        }
        if (input.pressed || input.released || input.cancel) {
            /* Overflow cannot strand a captured gesture. Retire it explicitly
             * rather than replaying an incomplete edge sequence as a click. */
            a->input_count=1; a->inputs[0]=(UiWidgetInput){.x=input.x,.y=input.y,.cancel=true};
        }
        a->model.last_result = APP_FULL; return APP_FULL;
    }
    a->inputs[a->input_count++] = input; return APP_OK;
}
void app_scroll(App *a, double delta)
{
    if (!a || !a->alive || !isfinite(delta)) { return; }
    a->widgets.lists[0].scroll = fmax(0,a->widgets.lists[0].scroll+delta);
    app_lyrics_hold(a);
    if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_INPUT); }
}
AppResult app_key(App *a, AppKey key, bool shift)
{
    if (!a || !a->alive || key > APP_KEY_PAGE_DOWN) { return APP_INVALID; }
    if (key == APP_KEY_BACK) { return app_enqueue(a,(AppAction){.type=APP_BACK}); }
    if (!a->control_count) { return APP_PENDING; }
    uint32_t focused = 0;
    for (uint32_t i = 0; i < a->control_count; ++i) { if (a->controls[i].id.id == a->widgets.focus) { focused = i; break; } }
    if (key == APP_KEY_TAB) {
        for (uint32_t i = 0; i < a->control_count; ++i) {
            focused = shift ? (focused+a->control_count-1)%a->control_count : (focused+1)%a->control_count;
            if (!a->controls[focused].disabled) { a->widgets.focus = a->controls[focused].id.id; return APP_OK; }
        }
        return APP_CANCELLED;
    }
    AppControl *control = &a->controls[focused];
    if (control->disabled) { return APP_CANCELLED; }
    if (control->id.id==CLAY_ID("AppVolume").id) { return app_volume_key(a,key); }
    if (a->model.location.screen==APP_LYRICS && control->id.id==CLAY_ID("LyricsList").id) {
        if (key==APP_KEY_UP || key==APP_KEY_DOWN || key==APP_KEY_PAGE_UP || key==APP_KEY_PAGE_DOWN || key==APP_KEY_HOME || key==APP_KEY_END) {
            if (key==APP_KEY_HOME) { a->widgets.lists[0].scroll=0; }
            else if (key==APP_KEY_END) { a->widgets.lists[0].scroll=48.0*LYRICS_LINE_CAP; }
            else { app_scroll(a,(key==APP_KEY_UP || key==APP_KEY_PAGE_UP ? -1 : 1)*(key==APP_KEY_PAGE_UP || key==APP_KEY_PAGE_DOWN ? 240 : 48)); }
            app_lyrics_hold(a); return APP_OK;
        }
    }
    if (a->catalog.page && control->action.type==APP_CATALOG_SELECT) {
        UiVirtualList *list=&a->widgets.lists[0];
        if (control->action.value>=0) { list->focus=app_catalog_key(a,(uint32_t)control->action.value); }
        UiNav nav;
        switch (key) {
        case APP_KEY_UP: nav=UI_NAV_UP; break; case APP_KEY_DOWN: nav=UI_NAV_DOWN; break;
        case APP_KEY_HOME: nav=UI_NAV_HOME; break; case APP_KEY_END: nav=UI_NAV_END; break;
        case APP_KEY_PAGE_UP: nav=UI_NAV_PAGE_UP; break; case APP_KEY_PAGE_DOWN: nav=UI_NAV_PAGE_DOWN; break;
        default: nav=(UiNav)-1; break;
        }
        if ((int)nav>=0) {
            (void)ui_virtual_navigate(list,a->catalog.page->count,APP_CATALOG_ROW,app_catalog_viewport(a),nav,true,
                app_catalog_key,app_catalog_index,a);
            a->widgets.focus=CLAY_ID("CatalogList").id; return APP_OK;
        }
        if (key==APP_KEY_ENTER) {
            uint32_t index; return app_catalog_index(a,list->focus,&index) ?
                app_enqueue(a,(AppAction){.type=APP_CATALOG_SELECT,.value=index}) : APP_CANCELLED;
        }
    }
    if (a->model.location.screen==APP_PLAYLIST && control->action.type==APP_TRACK_PLAY) {
        Playlist *p=a->services.playlist; if (!p || !p->ready || p->pending) { return APP_PENDING; }
        UiVirtualList *list=&a->widgets.lists[0];
        if (control->action.value>=1) { list->focus=(uint64_t)control->action.value; }
        UiNav nav;
        switch (key) {
        case APP_KEY_UP: nav=UI_NAV_UP; break; case APP_KEY_DOWN: nav=UI_NAV_DOWN; break;
        case APP_KEY_HOME: nav=UI_NAV_HOME; break; case APP_KEY_END: nav=UI_NAV_END; break;
        case APP_KEY_PAGE_UP: nav=UI_NAV_PAGE_UP; break; case APP_KEY_PAGE_DOWN: nav=UI_NAV_PAGE_DOWN; break;
        default: nav=(UiNav)-1; break;
        }
        if ((int)nav>=0) {
            (void)ui_virtual_navigate(list,p->count,40,fmin(360,fmax(0,a->geometry.viewport-104)),nav,true,playlist_key,playlist_index,p);
            (void)playlist_select(p,list->selection,false); a->widgets.focus=CLAY_ID("PlaylistList").id; return APP_OK;
        }
        if (key==APP_KEY_ENTER) { return app_enqueue(a,(AppAction){.type=APP_TRACK_PLAY,.value=(double)list->focus}); }
    }
    if (control->id.id == CLAY_ID("AppSeek").id) {
        if (a->widgets.seeks[0].dragging) { return APP_CANCELLED; }
        double value = a->widgets.seeks[0].value;
        double step = a->model.player.duration_frames ? 5.0 * a->model.player.rate / (double)a->model.player.duration_frames : 0;
        if (key == APP_KEY_HOME) { value = 0; } else if (key == APP_KEY_END) { value = 1; }
        else if (key == APP_KEY_LEFT) { value -= step; } else if (key == APP_KEY_RIGHT) { value += step; }
        else { return APP_CANCELLED; }
        return app_enqueue(a,(AppAction){.type=APP_SEEK,.value=fmin(1,fmax(0,value))});
    }
    if (a->model.page && (a->model.location.screen==APP_LIBRARY || a->model.location.screen==APP_SHELF) &&
        control->action.type==APP_SELECT) { return app_browse_key(a,key,control); }
    return key == APP_KEY_ENTER ? app_enqueue(a,control->action) : APP_CANCELLED;
}
