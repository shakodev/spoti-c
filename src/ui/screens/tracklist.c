#include "playlist.h"
#include "ui/app.h"
#include <math.h>
#include <string.h>

bool app_track_control(App *a, Clay_ElementId id, Clay_String name, float width, AppAction action, bool disabled)
{
    if (a->control_count==APP_CONTROLS) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return false; }
    a->controls[a->control_count++]=(AppControl){id,action,disabled};
    bool hit=ui_button(&a->widgets,id,app_text(a,name),width,28,disabled);
    if (hit) { (void)app_enqueue(a,action); } return hit;
}

static void cell(const char *s, float width, uint16_t size, uint32_t slot)
{
    CLAY(CLAY_IDI("PlaylistCellClip",slot),{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(28)}},.clip={.horizontal=true,.vertical=true}}) {
        app_label((Clay_String){false,(int32_t)strlen(s),s},size,(Clay_Color){205,205,205,255});
    }
}
static void row(void *context, uint32_t index, uint64_t key)
{
    App *a=context; Playlist *p=a->services.playlist; uint32_t slot=p->rendered_rows++;
    UiListRange visible;
    (void)ui_virtual_range(p->count,40,a->widgets.lists[0].scroll,fminf(360,fmaxf(0,a->geometry.viewport-104)),2,&visible);
    if (index<visible.visible_first || index>=visible.visible_end) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(40)}}}) { } return;
    }
    uint32_t ordinal=p->map[index]; const TrackRow *r=playlist_row(p,ordinal);
    float width=a->geometry.main-48, duration=48, title=(width-duration)*0.42f, artist=(width-duration)*0.28f;
    const char *name=r ? p->strings+r->title : "Loading metadata...";
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(40)},.childGap=2,.padding={4,4,5,5}}}) {
        bool disabled=p->pending || !r || !(r->flags&TRACK_PLAYABLE);
        Clay_ElementId id=CLAY_IDI("PlaylistTrack",ordinal); AppAction action={.type=APP_TRACK_PLAY,.value=(double)key};
        /* Only noninteractive clipping boxes reuse viewport slots. Semantic
         * row/button IDs still identify original playlist occurrences. */
        CLAY(CLAY_IDI("PlaylistCellClip",slot*4),{.layout={.sizing={CLAY_SIZING_FIXED(title),CLAY_SIZING_FIXED(28)}},
            .clip={.horizontal=true,.vertical=true}}) {
            (void)app_track_control(a,id,(Clay_String){false,(int32_t)strlen(name),name},title,action,disabled);
        }
        cell(r ? p->strings+r->artist : "",artist,14,slot*4+1);
        cell(r ? p->strings+r->album : "",width-duration-title-artist-6,14,slot*4+2);
        cell(r ? p->strings+r->time : "--:--",duration,14,slot*4+3);
    }
}
static uint64_t key(void *context, uint32_t index) { return playlist_key(((App *)context)->services.playlist,index); }
void app_tracklist(App *a)
{
    Playlist *p=a->services.playlist; p->rendered_rows=0;
    float width=a->geometry.main-48, duration=48, title=(width-duration)*0.42f, artist=(width-duration)*0.28f;
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(24)},.childGap=2}}) {
        cell("Title",title,14,256); cell("Artist",artist,14,257);
        cell("Album",width-duration-title-artist-6,14,258); cell("Time",duration,14,259);
    }
    UiListRange range;
    a->widgets.lists[0].selection=p->selected;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("PlaylistList"),&a->widgets.lists[0],p->ready ? p->count : 0,
        40,fminf(360,fmaxf(0,a->geometry.viewport-104)),2,key,row,a,&range);
    if (!p->pending && a->widgets.lists[0].selection!=p->selected && a->widgets.lists[0].selection) {
        (void)playlist_select(p,a->widgets.lists[0].selection,false);
    }
    if (a->control_count<APP_CONTROLS) {
        a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("PlaylistList"),.action={.type=APP_TRACK_PLAY},.disabled=p->pending};
    }
}
void app_playlist(App *a)
{
    Playlist *p=a->services.playlist;
    if (!p || !p->active) { app_message(a,CLAY_STRING("Playlist source unavailable"),APP_FAILURE); return; }
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},.childGap=6}}) {
        app_label(CLAY_STRING("Filter"),14,(Clay_Color){205,205,205,255});
        ui_text_input(&a->widgets,CLAY_ID("PlaylistFilter"),&a->widgets.texts[0],fmaxf(60,a->geometry.main-258));
        if (a->control_count<APP_CONTROLS) {
            a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("PlaylistFilter"),.action={.type=APP_TRACK_QUERY}};
        }
        (void)app_button(a,CLAY_ID("PlaylistSort"),p->sort==TRACK_SOURCE_ORDER ? CLAY_STRING("Order") :
            p->sort==TRACK_TITLE_ORDER ? CLAY_STRING("Title") : CLAY_STRING("Time"),52,
            (AppAction){.type=APP_TRACK_SORT,.value=(p->sort+1)%3},false);
        (void)app_button(a,CLAY_ID("PlaylistPlay"),CLAY_STRING("Play"),52,
            (AppAction){.type=APP_TRACK_PLAY,.value=(double)p->selected},!p->selected_visible || !p->selected_playable || p->pending);
    }
    app_label((Clay_String){false,(int32_t)strlen(p->summary),p->summary},12,(Clay_Color){190,190,190,255});
    if (p->result!=TRACK_OK && p->result!=TRACK_PENDING) {
        app_message(a,CLAY_STRING("Index unavailable. Retry this playlist."),APP_FAILURE);
        (void)app_button(a,CLAY_ID("PlaylistRetry"),CLAY_STRING("Retry"),64,(AppAction){.type=APP_RETRY},false);
    }
    if (p->ready && !p->count) { app_label(CLAY_STRING("No matching tracks"),16,(Clay_Color){210,210,210,255}); }
    app_tracklist(a);
    if (p->source.total>TRACK_INDEX_CAP) {
        CLAY_AUTO_ID({.layout={.childGap=8}}) {
            (void)app_button(a,CLAY_ID("PlaylistPreviousWindow"),CLAY_STRING("Prev 10k"),90,
                (AppAction){.type=APP_TRACK_WINDOW,.value=-1},!p->base || p->pending);
            (void)app_button(a,CLAY_ID("PlaylistNextWindow"),CLAY_STRING("Next 10k"),90,
                (AppAction){.type=APP_TRACK_WINDOW,.value=1},p->source.total-p->base<=TRACK_INDEX_CAP || p->pending);
        }
    }
}
