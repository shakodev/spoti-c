#include "app_shell_support.h"
#include "support/ui_heap_watch.h"
#include "ui/font_roles.h"
#include "ui/screens/playlist.h"
#include "ui/app_desktop.h"
#include <math.h>
#include <stdarg.h>
static bool dense_art_all;
static unsigned transfers;
static PlayerTransfer transferred;
static PlayerResult submit(void *context, const PlayerCommand *command)
{
    (void)context; CHECK(!app.widgets.in_frame);
    if (command->type==PLAYER_TRANSFER) { CHECK(command->transfer); transferred=*command->transfer; ++transfers; }
    return PLAYER_OK;
}

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
            for (unsigned item=0;item<6;++item) {
                const char *kind=item==1 ? "Playlist" : item==2 ? "Album" : item==3 ? "Artist" : "Track";
                const char *uri=item==1 ? "playlist" : item==2 ? "album" : item==3 ? "artist" : "track";
                unsigned identity=shelf*6+item+1;
                append(&r,&bytes,"%s{\"content\":{\"data\":{\"__typename\":\"%s\","
                    "\"uri\":\"spotify:%s:%022u\",\"name\":\"Card %u\"",
                    item ? "," : "",kind,uri,identity,shelf*6+item);
                /* Exact local synthetic sources; no network fetch is installed
                 * by the CPU fixture. The visual fixture injects PNG bytes. */
                if (item!=4 || dense_art_all) {
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
            if (i%5!=4 || dense_art_all) { append(&r,&bytes,",\"imageUri\":\"https://example.test/shell-art-%u.png\"",i+1); }
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
    shell_open(NULL); app.services.submit=submit;
    CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_dense_page(true);
    const float dpi[]={96,120,144,192}, widths[]={400,1100,1800};
    for (unsigned scale=0;scale<4;++scale) {
        for (unsigned size=0;size<3;++size) {
            app.widgets.lists[0].scroll=1;
            (void)shell_layout(widths[size]*dpi[scale]/96,1400*dpi[scale]/96,dpi[scale]);
            uint32_t columns=app_browse_columns(&app);
            CHECK(columns>=1 && columns<=6); image_bounds();
            CHECK(app.browse.visible_count>0 && control(CLAY_IDI("ShelfMore",0)));
            Clay_ElementData first=Clay_GetElementData(CLAY_IDI("HomeCard",0));
            CHECK(first.found && first.boundingBox.width>100 && first.boundingBox.height>148);
            for (uint32_t i=0;i<app.browse.visible_count;++i) {
                AppVisibleImage image=app.browse.visible[i];
                CHECK(image.shelf<3 && image.index<columns);
                CHECK(control(CLAY_IDI("HomeCard",image.shelf*8+image.index)));
            }
            if (size==2) { CHECK(columns==6 && app.browse.visible_count==18); }
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
static TrackFilter play_filter;
static Playlist play_list;
static TrackResult unused_read(void *context, uint32_t first, uint32_t count, TrackRecord *out)
{
    (void)context; (void)first; (void)count; (void)out; CHECK(false); return TRACK_IO;
}
static void play_open(bool home)
{
    memset(&play_filter,0,sizeof(play_filter)); atomic_init(&play_filter.phase,0);
    playlist_init(&play_list,&play_filter); shell_open(NULL);
    app.services.playlist=&play_list; app.services.submit=submit; transfers=0;
    CHECK(app_session(&app,true,true,0)==APP_PENDING);
    if (!home) { shell_action(APP_GO_LIBRARY); }
    shell_dense_page(home); (void)shell_layout(1800,1400,96);
}
static void ready(uint32_t count)
{
    Playlist *p=&play_list;
    /* Immutable worker completion represented at the UI boundary. Every row is
     * cached, so preparation cannot launch an I/O job in this CPU-only test. */
    p->source=(TrackSource){.read=unused_read,.total=count,.complete=true,.revision="browse-play"};
    strcpy_s(p->source.uri,sizeof(p->source.uri),p->uri);
    p->ready=true; p->pending=p->need_filter=false; p->result=TRACK_OK;
    p->cache_generation=p->generation; p->count=p->row_count=count; p->first_playable=count>1 ? 2 : 0;
    memcpy(p->strings,"\0First playable\0Artist\0",23);
    for (uint32_t i=0;i<count;++i) {
        p->map[i]=i; p->rows[i]=(TrackRow){.ordinal=i,.title=1,.artist=16};
        p->filter->index[i]=(TrackIdentity){.flags=(uint8_t)(TRACK_ID | (i ? TRACK_PLAYABLE : 0))};
        p->filter->index[i].id[15]=(uint8_t)(i+1);
    }
}
static AppAction playlist_action(bool home)
{
    Clay_ElementId card=home ? CLAY_IDI("HomeCard",1) : CLAY_IDI("LibraryItem",1);
    app.widgets.focus=card.id; (void)shell_layout(1800,1400,96);
    Clay_ElementId play=Clay__HashStringWithOffset(CLAY_STRING("CardPlay"),0,card.id);
    CHECK(control(card) && control(card)->action.type==APP_SELECT);
    CHECK(control(play) && control(play)->action.type==APP_BROWSE_PLAY && !control(play)->disabled);
    return control(play)->action;
}
static void pointer(Clay_BoundingBox box, bool pressed)
{
    UiWidgetInput input={.x=box.x+box.width/2,.y=box.y+box.height/2,
        .pressed=pressed,.released=!pressed,.down=pressed};
    CHECK(app_layout(&app,1800,1400,96,input).length>0 && app.widgets.error==UI_WIDGET_OK);
}
static void pending_play(bool home)
{
    play_open(home); CHECK(app_enqueue(&app,playlist_action(home))==APP_OK);
    CHECK(app_update(&app,0)==APP_PENDING && app.model.location.screen==APP_PLAYLIST);
    CHECK(app.browse.play_route==app.model.generation && !transfers);
    (void)app_update(&app,0); CHECK(!transfers);
}
static void playlist_cards(void)
{
    play_open(true); AppAction action=playlist_action(true);
    Clay_ElementId card=CLAY_IDI("HomeCard",1);
    Clay_ElementId play=Clay__HashStringWithOffset(CLAY_STRING("CardPlay"),0,card.id);
    Clay_BoundingBox box=Clay_GetElementData(card).boundingBox;
    box.y+=box.height-24; box.height=16; /* Card body navigates without play. */
    pointer(box,true); pointer(box,false);
    CHECK(app.action_count==1 && app.actions[0].type==APP_SELECT);
    (void)app_update(&app,0); CHECK(!app.browse.play_route); ready(3);
    (void)app_update(&app,0); CHECK(!transfers); shell_close();

    play_open(true); (void)playlist_action(true);
    box=Clay_GetElementData(play).boundingBox; pointer(box,true);
    CHECK(app.widgets.active==play.id && !app.action_count);
    pointer(box,false); CHECK(app.action_count==1 && app.actions[0].type==APP_BROWSE_PLAY);
    (void)app_update(&app,0); CHECK(app.browse.play_route==app.model.generation && !transfers);
    ready(3); (void)app_update(&app,0);
    CHECK(transfers==1 && transferred.play && transferred.count==2);
    CHECK(transferred.ids[0][15]==2 && transferred.ids[1][15]==3);
    CHECK(!app.browse.play_route && !strcmp(app.model.playing.title,"First playable"));
    (void)app_update(&app,0); CHECK(transfers==1); shell_close();

    play_open(false); (void)playlist_action(false);
    card=CLAY_IDI("LibraryItem",1); play=Clay__HashStringWithOffset(CLAY_STRING("CardPlay"),0,card.id);
    app.widgets.focus=card.id; CHECK(app_key(&app,APP_KEY_TAB,false)==APP_OK && app.widgets.focus==play.id);
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,0);
    ready(3); (void)app_update(&app,0); CHECK(transfers==1); shell_close();

    play_open(true); action=playlist_action(true);
    CHECK(app_enqueue(&app,(AppAction){.type=APP_GO_LIBRARY})==APP_OK && app_enqueue(&app,action)==APP_OK);
    CHECK(app_update(&app,0)==APP_STALE && app.model.location.screen==APP_LIBRARY);
    CHECK(!app.browse.play_route && !transfers); shell_close();

    pending_play(true); uint64_t old_route=play_list.route; TrackSource stale={.read=unused_read,.revision="old"};
    strcpy_s(stale.uri,sizeof(stale.uri),play_list.uri); shell_action(APP_GO_HOME);
    CHECK(!app.browse.play_route && playlist_source(&play_list,&stale,old_route)==TRACK_STALE);
    CHECK(!transfers); shell_close();
    pending_play(false); CHECK(app_refresh(&app)==APP_PENDING); ready(3); (void)app_update(&app,0);
    CHECK(!app.browse.play_route && !transfers); shell_close();
    pending_play(true); shell_action(APP_TRACK_SORT); ready(3); (void)app_update(&app,0);
    CHECK(!app.browse.play_route && !transfers); shell_close();
    pending_play(true); ready(0); (void)app_update(&app,0);
    CHECK(!app.browse.play_route && !transfers && app.model.last_result==APP_UNAVAILABLE); shell_close();
    pending_play(true); app.model.pending=false; app.model.status=APP_ERROR;
    (void)app_update(&app,0); CHECK(!app.browse.play_route); ready(3); (void)app_update(&app,0);
    CHECK(!transfers); shell_close();
    pending_play(true); shell_action(APP_SIGN_OUT);
    CHECK(!app.browse.play_route && !transfers && !app.model.logged_in); shell_close();
}
static unsigned textures;
static ArtResult no_fetch(void *context, const wchar_t *url, void *buffer, size_t capacity, size_t *bytes, const CancelToken *cancel)
{
    (void)context; (void)url; (void)buffer; (void)capacity; (void)bytes; (void)cancel;
    CHECK(false); return ART_INVALID;
}
static ArtResult texture_upload(void *context, uint32_t width, uint32_t height, const unsigned char *pixels, UiTexture *out)
{
    (void)context; CHECK(width && height && pixels); *out=++textures; return ART_OK;
}
static void texture_release(void *context, UiTexture texture) { (void)context; CHECK(texture>0); }
static bool texture_valid(void *context, UiTexture texture) { (void)context; return texture>0 && texture<=textures; }
static _Alignas(16) unsigned char art_state[65536], art_decode[ART_DECODE_BYTES], art_core[MEMORY_BUDGET_CORE];
static unsigned char art_pixels[ART_UPLOAD_BYTES];
static void retained_sidebar(void)
{
    static AppRootlistPage page;
    memset(&page,0,sizeof(page)); atomic_init(&page.references,1);
    page.generation=app.model.generation; page.count=16; page.total=32; page.next=16;
    for (uint32_t i=0;i<page.count;++i) {
        page.items[i].available=true;
        CHECK(sprintf_s(page.items[i].uri,sizeof(page.items[i].uri),"spotify:playlist:%022u",i+100)>0);
        CHECK(sprintf_s(page.items[i].name,sizeof(page.items[i].name),"Sidebar %u",i)>0);
        CHECK(sprintf_s(page.items[i].image,sizeof(page.items[i].image),"https://example.test/side-%u.png",i)>0);
    }
    app.sidebar.page=&page;
}
static void visible_admission(void)
{
    Arena core={0}; Worker *worker=NULL; Artwork *art=NULL;
    CHECK(arena_init(&core,art_core,sizeof(art_core)) && worker_start(&core,4,(CoreClock){0},&worker)==CORE_OK);
    ArtworkGpu gpu={texture_upload,texture_release,texture_valid,NULL};
    ArtworkConfig config={.worker=worker,.fetch=no_fetch,.gpu=gpu,.decode_workspace=art_decode,.upload=art_pixels};
    CHECK(artwork_size()<=sizeof(art_state) && artwork_init(art_state,sizeof(art_state),&config,&art)==ART_OK);
    dense_art_all=true;
    for (unsigned library=0;library<2;++library) {
        shell_open(NULL); app.services.artwork=art; app.services.submit=submit; app.services.playlist=&play_list;
        CHECK(app_session(&app,true,true,0)==APP_PENDING);
        if (library) { shell_action(APP_GO_LIBRARY); }
        shell_dense_page(!library); app.model.location.grid=true; retained_sidebar();
        const float dpi[]={96,192,768};
        for (unsigned scale=0;scale<3;++scale) {
            app.widgets.lists[0].scroll=1;
            app.widgets.focus=(library ? CLAY_IDI("LibraryItem",1) : CLAY_IDI("HomeCard",1)).id;
            (void)app_layout(&app,1800*dpi[scale]/96,1400*dpi[scale]/96,dpi[scale],(UiWidgetInput){.x=-10,.y=-10});
            if (app.widgets.error!=UI_WIDGET_OK) {
                fprintf(stderr,"dense combined layout: library=%u dpi=%g controls=%u labels=%u declarations=%u error=%u\n",
                    library,(double)dpi[scale],app.control_count,app.label_count,app.widgets.declarations,app.widgets.error);
            }
            CHECK(app.widgets.error==UI_WIDGET_OK);
            Clay_ElementId other=library ? CLAY_IDI("LibraryItem",4) : CLAY_IDI("HomeCard",0);
            Clay_BoundingBox other_box=Clay_GetElementData(other).boundingBox;
            app.now.queue_open=true; app.now.queue.count=6;
            (void)app_layout(&app,1800*dpi[scale]/96,1400*dpi[scale]/96,dpi[scale],
                (UiWidgetInput){.x=other_box.x+other_box.width/2,.y=other_box.y+12});
            CHECK(app.widgets.error==UI_WIDGET_OK && control(CLAY_ID("QueueClose")));
            CHECK(control(Clay__HashStringWithOffset(CLAY_STRING("CardPlay"),0,other.id)));
            app.now.queue_open=false;
            CHECK(app.browse.visible_count==18 && app.sidebar.visible_count==10);
            app_sidebar_prepare(&app); app_browse_prepare(&app); CHECK(app.sidebar.cover_count==10);
            CHECK(app.image_count==18); /* Includes the partial third row and ART_FULL requests awaiting worker capacity. */
            uint32_t bytes=56*56*4, side=(uint32_t)fminf(256,ceilf(48*dpi[scale]/96));
            bytes+=10*side*side*4;
            TextureCache cache; texture_cache_init(&cache,gpu); textures=0;
            for (uint32_t i=0;i<29;++i) {
                uint32_t edge=i<18 ? app.browse.visible[i].size : i<28 ? side : 56;
                if (i<18) {
                    bytes+=edge*edge*4; CHECK(app.images[i].id.size==edge);
                    CHECK(app.images[i].result==ART_PENDING || app.images[i].result==ART_FULL);
                }
                ArtworkId id={.size=edge,.generation=app.model.generation}; id.content[0]=(uint8_t)(i+1);
                ArtworkHandle handle;
                CHECK(texture_cache_insert(&cache,id,edge,edge,art_pixels,&handle)==ART_OK);
            }
            CHECK(bytes<=ART_GPU_BYTES && cache.bytes==bytes && cache.uploads==29);
            texture_cache_clear(&cache);
        }
        shell_close();
    }
    dense_art_all=false; CHECK(artwork_stop(art)==ART_OK);
    CHECK(worker_stop(worker)==CORE_OK && worker_join(worker,5000)==CORE_OK); arena_dispose(&core);
}
void shell_browse(void)
{
    home_density(); library_projection(); library_grid(); playlist_cards(); visible_admission();
    printf("browse state=%zu; app + media=%zu / %u bytes\n",sizeof(App),
        sizeof(App)+sizeof(MediaControls)+sizeof(MediaWinrt),(unsigned)MEMORY_BUDGET_UI_STATE);
}
#ifdef APP_BROWSE_TEST_MAIN
static DWORD WINAPI browse_run(void *context) { (void)context; shell_browse(); return 0; }
int main(void)
{
    HANDLE thread=CreateThread(NULL,0,browse_run,NULL,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread,60000)==WAIT_OBJECT_0);
    DWORD result; CHECK(GetExitCodeThread(thread,&result) && CloseHandle(thread)); return (int)result;
}
#endif
