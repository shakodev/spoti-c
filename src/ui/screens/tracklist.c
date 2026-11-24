#include "playlist.h"
#include "ui/app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

bool app_track_control(App *a, Clay_ElementId id, Clay_String name, float width, AppAction action, bool disabled, bool current)
{
    if (a->control_count==APP_CONTROLS) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return false; }
    a->controls[a->control_count++]=(AppControl){id,action,disabled};
    bool hit=ui_track_button(&a->widgets,id,app_text(a,name),width,disabled,current);
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
    (void)ui_virtual_range(p->count,UI_TRACK_HEIGHT,a->widgets.lists[0].scroll,app_playlist_viewport(a),2,&visible);
    if (index<visible.visible_first || index>=visible.visible_end) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(UI_TRACK_HEIGHT)}}}) { } return;
    }
    uint32_t ordinal=p->map[index]; const TrackRow *r=playlist_row(p,ordinal);
    float width=a->geometry.main-48, duration=48, ordinal_width=28;
    float title=(width-duration-ordinal_width)*(a->geometry.compact ? 1.0f : 0.65f);
    float album=width-duration-ordinal_width-title-8;
    const char *name=r ? p->strings+r->title : "Loading metadata...";
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(UI_TRACK_HEIGHT)},
        .childGap=2,.padding={4,4,4,4},.childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
        bool disabled=p->pending || !r || !(r->flags&TRACK_PLAYABLE);
        Clay_ElementId id=CLAY_IDI("PlaylistTrack",ordinal); AppAction action={.type=APP_TRACK_PLAY,.value=(double)key};
        /* Only noninteractive clipping boxes reuse viewport slots. Semantic
         * row/button IDs still identify original playlist occurrences. */
        char number[16]; (void)snprintf(number,sizeof(number),"%u",ordinal+1);
        cell(app_text(a,(Clay_String){false,(int32_t)strlen(number),number}).chars,ordinal_width,14,slot*4+1);
        CLAY(CLAY_IDI("PlaylistCellClip",slot*4),{.layout={.sizing={CLAY_SIZING_FIXED(title),CLAY_SIZING_FIXED(48)},
            .layoutDirection=CLAY_TOP_TO_BOTTOM},
            .clip={.horizontal=true,.vertical=true}}) {
            bool current=false;
            if (p->ready && !p->pending) {
                const TrackIdentity *identity=&p->filter->index[ordinal-p->base];
                current=a->model.player.state!=PLAYER_STOPPED && a->model.player.state!=PLAYER_ERROR &&
                    (identity->flags&TRACK_ID) && !memcmp(identity->id,a->model.player.track_id,16);
            }
            (void)app_track_control(a,id,(Clay_String){false,(int32_t)strlen(name),name},title,action,disabled,current);
            app_label((Clay_String){false,(int32_t)strlen(r ? p->strings+r->artist : ""),r ? p->strings+r->artist : ""},13,UI_SUBDUED);
        }
        if (!a->geometry.compact) { cell(r ? p->strings+r->album : "",album,14,slot*4+2); }
        cell(r ? p->strings+r->time : "--:--",duration,14,slot*4+3);
    }
}
static uint64_t key(void *context, uint32_t index) { return playlist_key(((App *)context)->services.playlist,index); }
void app_tracklist(App *a)
{
    Playlist *p=a->services.playlist; p->rendered_rows=0;
    float width=a->geometry.main-48, duration=48, ordinal_width=28;
    float title=(width-duration-ordinal_width)*(a->geometry.compact ? 1.0f : 0.65f);
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(24)},.childGap=2}}) {
        cell("#",ordinal_width,14,257); cell("Title",title,14,256);
        if (!a->geometry.compact) { cell("Album",width-duration-ordinal_width-title-8,14,258); }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(duration),CLAY_SIZING_FIXED(24)}}}) { ui_detail_icon(&a->widgets,UI_ICON_CLOCK,16,UI_SUBDUED); }
    }
    UiListRange range;
    a->widgets.lists[0].selection=p->selected;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("PlaylistList"),&a->widgets.lists[0],p->ready ? p->count : 0,
        UI_TRACK_HEIGHT,app_playlist_viewport(a),2,key,row,a,&range);
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
    float cover=a->geometry.compact ? 96.0f : 160.0f;
    CLAY(CLAY_ID("PlaylistHero"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(cover)},
        .childGap=16,.childAlignment={.y=CLAY_ALIGN_Y_BOTTOM}}}) {
        CLAY(CLAY_ID("PlaylistCover"),{.layout={.sizing={CLAY_SIZING_FIXED(cover),CLAY_SIZING_FIXED(cover)},
            .childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},.backgroundColor=UI_HIGHLIGHT,
            .cornerRadius=CLAY_CORNER_RADIUS(4)}) { ui_detail_icon(&a->widgets,UI_ICON_MUSIC,48,UI_SUBDUED); }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(cover)},.childGap=8,
            .layoutDirection=CLAY_TOP_TO_BOTTOM,.childAlignment={.y=CLAY_ALIGN_Y_BOTTOM}},.clip={.horizontal=true,.vertical=true}}) {
            app_label(CLAY_STRING("Playlist"),13,UI_TEXT);
            const char *title=a->model.location.entity.title[0] ? a->model.location.entity.title : "Playlist";
            app_heading((Clay_String){false,(int32_t)strlen(title),title},a->geometry.compact ? 28 : 48);
            char count[64]; (void)snprintf(count,sizeof(count),"%u songs",p->source.total);
            app_label(app_text(a,(Clay_String){false,(int32_t)strlen(count),count}),14,UI_SUBDUED);
        }
    }
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(56)},.childGap=8,
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
        uint64_t target=playlist_play_target(p);
        (void)app_component(a,CLAY_ID("PlaylistPlay"),CLAY_STRING(""),56,56,
            (AppAction){.type=APP_TRACK_PLAY,.value=(double)target},!target,
            (UiButtonStyle){.kind=UI_BUTTON_PRIMARY,.icon=UI_ICON_PLAY});
        ui_text_input(&a->widgets,CLAY_ID("PlaylistFilter"),&a->widgets.texts[0],fmaxf(60,a->geometry.main-200));
        if (a->control_count<APP_CONTROLS) {
            a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("PlaylistFilter"),.action={.type=APP_TRACK_QUERY}};
        }
        (void)app_button(a,CLAY_ID("PlaylistSort"),p->sort==TRACK_SOURCE_ORDER ? CLAY_STRING("Order") :
            p->sort==TRACK_TITLE_ORDER ? CLAY_STRING("Title") : CLAY_STRING("Time"),52,
            (AppAction){.type=APP_TRACK_SORT,.value=(p->sort+1)%3},false);
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
float app_playlist_viewport(const App *a)
{
    return fminf(336,fmaxf(56,a->geometry.viewport-(a->geometry.compact ? 96.0f : 160.0f)-136));
}
