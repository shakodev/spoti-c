#include "app_shell_support.h"
#include "support/ui_heap_watch.h"
#include "ui/font_roles.h"
#include <math.h>
#include <stdarg.h>

static AppControl *control(Clay_ElementId id)
{
    for (uint32_t i=0;i<app.control_count;++i) {
        if (app.controls[i].id.id==id.id) { return &app.controls[i]; }
    }
    return NULL;
}
static bool has_text(Clay_RenderCommandArray commands, const char *text)
{
    size_t bytes=strlen(text);
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT &&
            c->renderData.text.stringContents.length==(int32_t)bytes &&
            !memcmp(c->renderData.text.stringContents.chars,text,bytes)) { return true; }
    }
    return false;
}
static bool focus_border(Clay_RenderCommandArray commands, Clay_ElementId id)
{
    Clay_ElementData element=Clay_GetElementData(id); CHECK(element.found);
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType!=CLAY_RENDER_COMMAND_TYPE_BORDER || c->renderData.border.width.left!=2) { continue; }
        Clay_BoundingBox box=c->boundingBox, expected=element.boundingBox;
        if (fabsf(box.x-expected.x)<0.01f && fabsf(box.y-expected.y)<0.01f &&
            fabsf(box.width-expected.width)<0.01f && fabsf(box.height-expected.height)<0.01f) { return true; }
    }
    return false;
}
static void append(CollectionRequest *r, size_t *bytes, const char *format, ...)
{
    CHECK(*bytes<r->capacity);
    va_list arguments; va_start(arguments,format);
    int count=vsnprintf(r->body+*bytes,r->capacity-*bytes,format,arguments);
    va_end(arguments);
    CHECK(count>=0 && (size_t)count<r->capacity-*bytes); *bytes+=(size_t)count;
}
void shell_dense_page(bool home)
{
    CollectionRequest r;
    CHECK(collection_fetch(collection,latest.kind,latest.cursor,latest.generation,&r)==COLLECTION_PENDING);
    size_t bytes=0;
    if (home) {
        append(&r,&bytes,"{\"data\":{\"home\":{\"__typename\":\"HomeResponsePayload\","
            "\"sectionContainer\":{\"sections\":{\"totalCount\":8,\"items\":[");
        for (unsigned shelf=0;shelf<8;++shelf) {
            append(&r,&bytes,"%s{\"uri\":\"spotify:section:test%u\",\"data\":{"
                "\"__typename\":\"HomeGenericSectionData\",\"title\":{\"transformedLabel\":\"Shelf %u\"}},"
                "\"sectionItems\":{\"items\":[",shelf ? "," : "",shelf,shelf);
            for (unsigned item=0;item<5;++item) {
                const char *kind=item==1 ? "Playlist" : item==2 ? "Album" : item==3 ? "Artist" : "Track";
                const char *uri=item==1 ? "playlist" : item==2 ? "album" : item==3 ? "artist" : "track";
                unsigned identity=shelf*5+item+1;
                append(&r,&bytes,"%s{\"content\":{\"data\":{\"__typename\":\"%s\","
                    "\"uri\":\"spotify:%s:%022u\",\"name\":\"Card %u\"",
                    item ? "," : "",kind,uri,identity,shelf*5+item);
                /* Exact local synthetic sources; no network fetch is installed
                 * by the CPU fixture. The visual fixture injects PNG bytes. */
                if (item!=4) {
                    const char *open=item==1 ? ",\"images\":{\"items\":[{\"sources\":[" :
                        item==2 ? ",\"coverArt\":{\"sources\":[" :
                        item==3 ? ",\"visuals\":{\"avatarImage\":{\"sources\":[" :
                        ",\"albumOfTrack\":{\"coverArt\":{\"sources\":[";
                    const char *close=item==1 ? "]}]}" : item==2 ? "]}" : "]}}";
                    append(&r,&bytes,"%s{\"url\":\"https://example.test/shell-art-%u.png\","
                        "\"width\":4,\"height\":2}%s",open,identity,close);
                }
                append(&r,&bytes,"}}}");
            }
            append(&r,&bytes,"]}}");
        }
        append(&r,&bytes,"]}}}}}");
    } else {
        append(&r,&bytes,"{\"statusCode\":200,\"header\":{\"totalCount\":128,\"isLoading\":false},\"entity\":[");
        for (unsigned i=0;i<64;++i) {
            const char *kind=i%3==0 ? "playlist" : i%3==1 ? "album" : "artist";
            const char *title=i%3==0 ? "Playlist" : i%3==1 ? "Album" : "\\u05e9\\u05dc\\u05d5\\u05dd";
            append(&r,&bytes,"%s{\"entityInfo\":{\"name\":\"%s %u\",\"uri\":\"spotify:%s:%022u\"",
                i ? "," : "",title,i,kind,i+1);
            if (i%5!=4) { append(&r,&bytes,",\"imageUri\":\"https://example.test/shell-art-%u.png\"",i+1); }
            append(&r,&bytes,"},\"%s\":{}}",kind);
        }
        append(&r,&bytes,"],\"pinnedEntity\":[]}");
    }
    const CollectionPage *page=NULL;
    CHECK(collection_complete(collection,r.ticket,COLLECTION_OK,200,bytes,&page)==COLLECTION_OK);
    CHECK(page && app_deliver(&app,latest,COLLECTION_OK,page)==APP_OK);
}
static void image_bounds(void)
{
    CHECK(app.browse.visible_count<=APP_BROWSE_VISIBLE);
    CHECK(app.label_count<=64 && app.control_count<=APP_CONTROLS);
    for (uint32_t i=0;i<app.browse.visible_count;++i) {
        AppVisibleImage image=app.browse.visible[i];
        CHECK(image.size>0 && image.size<=256);
        for (uint32_t j=0;j<i;++j) {
            CHECK(image.shelf!=app.browse.visible[j].shelf || image.index!=app.browse.visible[j].index);
        }
    }
}
static void home_density(void)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_dense_page(true);
    const float dpi[]={96,120,144,192}, widths[]={400,1100,1800};
    for (unsigned scale=0;scale<4;++scale) {
        for (unsigned size=0;size<3;++size) {
            app.widgets.lists[0].scroll=1;
            (void)shell_layout(widths[size]*dpi[scale]/96,1400*dpi[scale]/96,dpi[scale]);
            uint32_t columns=app_browse_columns(&app);
            CHECK(columns>=1 && columns<=5); image_bounds();
            CHECK(app.browse.visible_count>0 && control(CLAY_IDI("ShelfMore",0)));
            Clay_ElementData first=Clay_GetElementData(CLAY_IDI("HomeCard",0));
            CHECK(first.found && first.boundingBox.width>100 && first.boundingBox.height>148);
            for (uint32_t i=0;i<app.browse.visible_count;++i) {
                AppVisibleImage image=app.browse.visible[i];
                CHECK(image.shelf<3 && image.index<columns);
                CHECK(control(CLAY_IDI("HomeCard",image.shelf*8+image.index)));
            }
            if (size==2) { CHECK(app.browse.visible_count==3*app_browse_columns(&app)); }
        }
    }
    (void)shell_layout(1800,1400,96);
    Clay_ElementId track=CLAY_IDI("HomeCard",0), playlist=CLAY_IDI("HomeCard",1);
    Clay_ElementId track_play=Clay__HashStringWithOffset(CLAY_STRING("CardPlay"),0,track.id);
    Clay_ElementId playlist_play=Clay__HashStringWithOffset(CLAY_STRING("CardPlay"),0,playlist.id);
    CHECK(!Clay_GetElementData(track_play).found && !Clay_GetElementData(playlist_play).found);
    app.widgets.focus=track.id; (void)shell_layout(1800,1400,96);
    CHECK(Clay_GetElementData(track_play).found);
    app.widgets.focus=playlist.id; (void)shell_layout(1800,1400,96);
    CHECK(!Clay_GetElementData(playlist_play).found);
    CHECK(control(playlist)->action.entity.kind==COLLECTION_PLAYLIST_ITEM);
    CHECK(!control(playlist)->action.entity.playable);
    ui_heap_begin();
    for (unsigned i=0;i<128;++i) {
        app.widgets.lists[0].scroll=(i%8)*127.0;
        (void)shell_layout(1800,1400,96); image_bounds();
    }
    ui_heap_end(); shell_close();
}
static void library_projection(void)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING);
    shell_action(APP_GO_LIBRARY); shell_dense_page(false);
    const CollectionPage *page=app.model.page;
    (void)shell_layout(1100,900,96);
    CHECK(app.browse.count==64 && app.model.page==page && app.model.page->count==64);
    CHECK(control(CLAY_IDI("LibraryItem",1)) && control(CLAY_ID("AppMore")));
    CHECK(Clay_GetElementData(CLAY_IDI("LibraryItem",1)).boundingBox.height==64);
    CHECK(app_enqueue(&app,(AppAction){.type=APP_LIBRARY_FILTER,.value=2})==APP_OK);
    (void)app_update(&app,0); (void)shell_layout(1100,900,96);
    CHECK(app.browse.count==21 && app.browse.order[0]==1 && app.browse.order[1]==4);
    CHECK(control(CLAY_IDI("LibraryItem",2)) && !control(CLAY_IDI("LibraryItem",1)));
    CHECK(control(CLAY_IDI("LibraryItem",2))->action.value==1);
    unsigned before=requests;
    CHECK(ui_text_set(&app.widgets.texts[1],"ALBUM 4",7)==UI_WIDGET_OK);
    shell_action(APP_LIBRARY_QUERY); (void)shell_layout(1100,900,96);
    CHECK(app.browse.count==5 && app.browse.order[0]==4 && requests==before);
    CHECK(app.model.page==page && app.model.page->items[4].kind==COLLECTION_ALBUM);
    CHECK(ui_text_set(&app.widgets.texts[1],"no matching item",16)==UI_WIDGET_OK);
    shell_action(APP_LIBRARY_QUERY);
    Clay_RenderCommandArray commands=shell_layout(1100,900,96);
    CHECK(app.browse.count==0 && has_text(commands,"No matches on this loaded page."));
    CHECK(control(CLAY_ID("AppMore")) && !control(CLAY_ID("AppMore"))->disabled);
    CHECK(control(CLAY_ID("AppList")) && control(CLAY_ID("AppList"))->disabled);
    CHECK(app.browse.visible_count==0 && app.model.page==page && requests==before);
    shell_action(APP_MORE); CHECK(latest.cursor==64 && app.model.page==page);
    (void)shell_layout(1100,900,96); CHECK(control(CLAY_ID("AppMore"))->disabled);
    shell_close();
}
static void library_grid(void)
{
    shell_open(NULL); CHECK(app_session(&app,true,true,0)==APP_PENDING);
    shell_action(APP_GO_LIBRARY); shell_dense_page(false); shell_action(APP_LIBRARY_GRID);
    CHECK(app.model.location.grid);
    const float dpi[]={96,120,144,192}, widths[]={400,1100,1800};
    for (unsigned scale=0;scale<4;++scale) {
        for (unsigned size=0;size<3;++size) {
            app.widgets.lists[0].scroll=1;
            Clay_RenderCommandArray commands=shell_layout(widths[size]*dpi[scale]/96,1400*dpi[scale]/96,dpi[scale]);
            image_bounds();
            uint32_t columns=app_browse_columns(&app);
            CHECK(app.browse.visible_count==3*columns);
            Clay_ElementData list=Clay_GetElementData(CLAY_ID("AppList"));
            for (uint32_t i=0;i<app.browse.visible_count;++i) {
                AppVisibleImage image=app.browse.visible[i]; CHECK(image.shelf==UINT32_MAX);
                AppControl *item=control(CLAY_IDI("LibraryItem",image.index+1)); CHECK(item);
                Clay_ElementData bounds=Clay_GetElementData(item->id);
                CHECK(bounds.found && bounds.boundingBox.height>64);
                CHECK(bounds.boundingBox.x>=list.boundingBox.x &&
                    bounds.boundingBox.x+bounds.boundingBox.width<=list.boundingBox.x+list.boundingBox.width+0.1f);
            }
            bool rtl=false;
            for (int32_t i=0;i<commands.length;++i) {
                const Clay_RenderCommand *c=&commands.internalArray[i];
                if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT &&
                    (c->renderData.text.fontId&FONT_ROLE_RTL)) { rtl=true; }
            }
            CHECK(rtl);
        }
    }
    app.widgets.focus=CLAY_ID("AppList").id;
    CHECK(app_key(&app,APP_KEY_END,false)==APP_OK);
    Clay_RenderCommandArray focused=shell_layout(1800,1400,96);
    CHECK(app.widgets.lists[0].focus==64 && control(CLAY_IDI("LibraryItem",64)));
    CHECK((64-1)%app_browse_columns(&app)!=0); /* Focus is not the virtual row's first key. */
    for (uint32_t i=0;i<app.browse.visible_count;++i) {
        uint32_t key=app.browse.visible[i].index+1;
        CHECK(focus_border(focused,CLAY_IDI("LibraryItem",key))==(key==64));
    }
    CHECK(app_key(&app,APP_KEY_HOME,false)==APP_OK);
    (void)shell_layout(1800,1400,96); CHECK(app.widgets.lists[0].focus==1);
    ui_heap_begin();
    for (unsigned i=0;i<128;++i) {
        app.widgets.lists[0].scroll=i*71.0;
        (void)shell_layout(1800,1400,96); image_bounds();
    }
    ui_heap_end(); shell_close();
}
void shell_browse(void)
{
    home_density(); library_projection(); library_grid();
}
