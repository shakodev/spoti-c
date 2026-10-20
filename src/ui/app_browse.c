#include "app.h"
#include "collection_artwork.h"
#include "font_roles.h"
#include <math.h>
#include <string.h>

static bool route(const App *a)
{
    return a->model.page && (a->model.location.screen==APP_HOME ||
        a->model.location.screen==APP_LIBRARY || a->model.location.screen==APP_SHELF);
}
static unsigned char fold(unsigned char c) { return c>='A' && c<='Z' ? (unsigned char)(c+32) : c; }
static bool matches(JsonString name, const char *query)
{
    size_t n=strlen(query);
    if (!n) { return true; }
    if (!name.data || n>name.bytes) { return false; }
    for (size_t i=0;i<=name.bytes-n;++i) {
        size_t j=0;
        while (j<n && fold((unsigned char)name.data[i+j])==fold((unsigned char)query[j])) { ++j; }
        if (j==n) { return true; }
    }
    return false;
}
void app_browse_project(App *a)
{
    a->browse.count=0;
    if (!route(a)) { return; }
    for (uint32_t i=0;i<a->model.page->count;++i) {
        const CollectionItem *item=&a->model.page->items[i];
        uint32_t filter=a->model.location.selector;
        bool include=filter==0 || (filter==1 && (item->kind==COLLECTION_PLAYLIST_ITEM || item->kind==COLLECTION_LIKED)) ||
            (filter==2 && item->kind==COLLECTION_ALBUM) || (filter==3 && item->kind==COLLECTION_ARTIST);
        if (include && matches(item->name,a->model.location.query)) { a->browse.order[a->browse.count++]=(uint16_t)i; }
    }
}
uint32_t app_browse_columns(const App *a)
{
    return (uint32_t)fminf(5,fmaxf(1,floorf((a->geometry.main-20)/172)));
}
void app_browse_restore(App *a)
{
    (void)ui_text_set(&a->widgets.texts[1],a->model.location.query,(uint32_t)strlen(a->model.location.query));
    a->browse.surrogate=0;
}
void app_browse_begin(App *a) { a->browse.visible_count=0; }
void app_browse_end(App *a)
{
    AppBrowse *b=&a->browse;
    bool changed=b->generation!=a->model.generation || b->previous_count!=b->visible_count ||
        memcmp(b->previous,b->visible,b->visible_count*sizeof(b->visible[0]));
    if (!changed) { return; }
    memcpy(b->previous,b->visible,b->visible_count*sizeof(b->visible[0]));
    b->previous_count=b->visible_count; b->generation=a->model.generation;
    if (a->services.artwork) {
        if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_STATE); }
        if (a->services.wake) { a->services.wake(a->services.context); }
    }
}
void app_browse_prepare(App *a)
{
    if (!route(a) || !a->services.artwork) { return; }
    /* A delivered replacement page invalidates every old viewport index. */
    if (a->browse.generation!=a->model.generation) { return; }
    a->image_count=0;
    for (uint32_t i=0;i<a->browse.previous_count;++i) {
        AppVisibleImage v=a->browse.previous[i]; CollectionItem member;
        if (v.shelf==UINT32_MAX) {
            if (v.index>=a->model.page->count) { continue; }
            member=a->model.page->items[v.index];
        } else if (collection_shelf_item(a->model.page,v.shelf,v.index,&member)!=COLLECTION_OK) { continue; }
        (void)app_collection_image(a,&member,v.size);
    }
}
void app_browse_cover(App *a, const CollectionItem *item, uint32_t shelf, uint32_t index, float edge)
{
    AppEntity entity={0}; AppImage *image=NULL;
    if (app_entity(item,&entity)==APP_OK) {
        if (a->browse.visible_count<APP_BROWSE_VISIBLE) {
            a->browse.visible[a->browse.visible_count++]=(AppVisibleImage){shelf,index,(uint32_t)fminf(256,ceilf(edge*a->geometry.scale))};
        }
        for (uint32_t i=0;i<a->image_count;++i) {
            if (!strcmp(a->images[i].entity.uri,entity.uri)) { image=&a->images[i]; break; }
        }
    }
    /* Custom image is emitted only when admitted; placeholder is always useful. */
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(edge),CLAY_SIZING_FIXED(edge)},
        .childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},
        /* Clay emits this element's background after its custom command.
         * The custom renderer supplies its own image/placeholder background. */
        .backgroundColor=image ? (Clay_Color){0} : (Clay_Color){40,40,40,255},.cornerRadius=CLAY_CORNER_RADIUS(4),
        .custom={.customData=image}}) {
        if (!image || image->result!=ART_OK) {
            Clay_ElementId id=Clay__HashStringWithOffset(CLAY_STRING("CoverPlaceholder"),index,shelf);
            bool failed=image && image->result!=ART_PENDING && image->result!=ART_FULL;
            ui_icon(&a->widgets,id,failed ? UI_ICON_WARNING : image ? UI_ICON_MORE : UI_ICON_LIBRARY,
                fminf(36,edge*0.6f),failed ? (Clay_Color){255,184,77,255} : (Clay_Color){167,167,167,255},false);
        }
    }
}
static bool rtl(const char *s)
{
    for (const unsigned char *p=(const unsigned char *)s;*p;++p) {
        if ((*p>='A' && *p<='Z') || (*p>='a' && *p<='z')) { return false; }
        if (*p>=0xd6 && *p<=0xdb) { return true; }
    }
    return false;
}
static Clay_String kind_name(CollectionItemKind kind)
{
    switch (kind) {
    case COLLECTION_PLAYLIST_ITEM: case COLLECTION_LIKED: return CLAY_STRING("Playlist");
    case COLLECTION_ALBUM: return CLAY_STRING("Album");
    case COLLECTION_ARTIST: return CLAY_STRING("Artist");
    case COLLECTION_TRACK: return CLAY_STRING("Song");
    case COLLECTION_FOLDER: return CLAY_STRING("Folder");
    default: return CLAY_STRING("Unavailable");
    }
}
void app_browse_card(App *a, Clay_ElementId id, const CollectionItem *item,
    uint32_t shelf, uint32_t index, float width, float edge, bool list)
{
    AppEntity entity={0}; bool valid=app_entity(item,&entity)==APP_OK && item->available;
    if (a->control_count==APP_CONTROLS || !ui_widgets_declare(&a->widgets,id)) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return; }
    AppAction action={.type=APP_SELECT,.entity=entity,.value=index};
    a->controls[a->control_count++]=(AppControl){id,action,!valid};
    bool hit=ui_widgets_button_event(&a->widgets,id,!valid);
    if (shelf==UINT32_MAX && a->widgets.hover==id.id && a->widgets.input.pressed) {
        a->widgets.lists[0].focus=a->widgets.lists[0].selection=(uint64_t)a->model.page->offset+index+1;
    }
    if (hit) { (void)app_enqueue(a,action); }
    bool hover=a->widgets.hover==id.id, focus=a->widgets.focus==id.id ||
        (shelf==UINT32_MAX && a->widgets.focus==CLAY_ID("AppList").id &&
        a->widgets.lists[0].focus==(uint64_t)a->model.page->offset+index+1);
    uint32_t state=(!valid ? UI_CONTROL_DISABLED : 0) | (hover ? UI_CONTROL_HOVER : 0) | (focus ? UI_CONTROL_FOCUS : 0) |
        (a->widgets.active==id.id && a->widgets.input.down ? UI_CONTROL_PRESSED : 0);
    if (shelf==UINT32_MAX && a->widgets.lists[0].selection==(uint64_t)a->model.page->offset+index+1) { state|=UI_CONTROL_SELECTED; }
    UiComponentStyle style=ui_component_style(list ? UI_COMPONENT_ROW : UI_COMPONENT_CARD,state);
    bool direction=rtl(entity.title);
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(list ? 64 : edge+64)},
        .padding=CLAY_PADDING_ALL(8),.childGap=8,.layoutDirection=list ? CLAY_LEFT_TO_RIGHT : CLAY_TOP_TO_BOTTOM,
        .childAlignment={.y=list ? CLAY_ALIGN_Y_CENTER : CLAY_ALIGN_Y_TOP}},
        .backgroundColor=style.background,.cornerRadius=CLAY_CORNER_RADIUS(style.radius),
        .border={.color=style.outline,.width=CLAY_BORDER_ALL(style.border)},.clip={.horizontal=true,.vertical=true}}) {
        app_browse_cover(a,item,shelf,index,edge);
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(0),CLAY_SIZING_GROW(0)},.childGap=4,
            .layoutDirection=CLAY_TOP_TO_BOTTOM,.childAlignment={.x=direction ? CLAY_ALIGN_X_RIGHT : CLAY_ALIGN_X_LEFT}},
            .clip={.horizontal=true,.vertical=true}}) {
            Clay_String title=valid ? (Clay_String){false,(int32_t)strlen(entity.title),entity.title} : CLAY_STRING("Unavailable");
            ui_component_label(app_text(a,title),FONT_ROLE_BODY,14,style.foreground,direction);
            ui_component_label(kind_name(item->kind),FONT_ROLE_BODY,12,(Clay_Color){179,179,179,255},direction);
        }
        if (entity.playable && (hover || focus)) {
            Clay_ElementId play=Clay__HashStringWithOffset(CLAY_STRING("CardPlay"),0,id.id);
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(32),CLAY_SIZING_FIXED(32)},
                .childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},
                .floating={.attachTo=CLAY_ATTACH_TO_PARENT,.attachPoints={CLAY_ATTACH_POINT_RIGHT_TOP,CLAY_ATTACH_POINT_RIGHT_TOP},
                    .offset={-12,12},.pointerCaptureMode=CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH},
                .backgroundColor={30,215,96,255},.cornerRadius=CLAY_CORNER_RADIUS(16)}) {
                ui_icon(&a->widgets,play,UI_ICON_PLAY,20,(Clay_Color){0,0,0,255},false);
            }
        }
    }
}
void app_browse_query(App *a)
{
    app_label(CLAY_STRING("Search this loaded page"),12,(Clay_Color){179,179,179,255});
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(32)},.childGap=8}}) {
        ui_text_input(&a->widgets,CLAY_ID("LibraryQuery"),&a->widgets.texts[1],fmaxf(80,a->geometry.main-116));
        if (a->control_count<APP_CONTROLS) {
            a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("LibraryQuery"),.action={.type=APP_LIBRARY_FOCUS}};
        }
        (void)app_shell_button(a,CLAY_ID("LibraryGrid"),a->model.location.grid ? CLAY_STRING("List") : CLAY_STRING("Grid"),
            UI_ICON_NONE,UI_COMPONENT_SECONDARY,60,32,APP_LIBRARY_GRID,false,false);
    }
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},.childGap=4}}) {
        const Clay_String labels[]={CLAY_STRING("All"),CLAY_STRING("Playlists"),CLAY_STRING("Albums"),CLAY_STRING("Artists")};
        float width=fminf(76,fmaxf(36,(a->geometry.main-44)/4));
        for (uint32_t i=0;i<4;++i) {
            uint32_t before=a->control_count, queued=a->action_count;
            bool hit=app_shell_button(a,CLAY_IDI("LibraryFilter",i),labels[i],UI_ICON_NONE,UI_COMPONENT_TAB,
                width,28,APP_LIBRARY_FILTER,a->model.location.selector==i,false);
            if (a->control_count>before) { a->controls[before].action.value=i; }
            /* Pointer activation is already queued by app_shell_button. */
            if (hit && a->action_count>queued && a->actions[a->action_count-1].type==APP_LIBRARY_FILTER) {
                a->actions[a->action_count-1].value=i;
            }
        }
    }
}
