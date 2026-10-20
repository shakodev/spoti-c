#include "catalog_screens_test.h"
#include "ui/screens/playlist.h"
#include <stdio.h>
#include <wchar.h>

#define LINES "[{\"startTimeMs\":\"0\",\"words\":\"first\"},{\"startTimeMs\":\"500\",\"words\":\"second\"}]"
#define LYRICS(mode,cap) "{\"lyrics\":{\"provider\":\"fixture\",\"providerDisplayName\":\"Fixture\",\"providerLyricsId\":\"id\",\"syncType\":\"" mode "\",\"capStatus\":\"" cap "\",\"lines\":" LINES ",\"previewLines\":[]}}"
static void query(void) { cs_query("music"); cs_tick(cs_now+250); }
static void responses(void)
{
    cs_open(false);
    query(); AppRequest old=cs_latest; unsigned n=cs_requests;
    cs_query("changed"); CHECK(cs_cancels>0 && !cs_app.model.pending && cs_app.model.status==APP_LOADING);
    CHECK(app_catalog_deliver(&cs_app,old,CATALOG_ERROR,NULL)==APP_STALE && cs_app.catalog.page==NULL);
    cs_tick(cs_now+249); CHECK(cs_requests==n); cs_tick(cs_now+1); CHECK(cs_requests==n+1);
    cs_response("{\"data\":{\"searchV2\":{\"topResultsV2\":{\"itemsV2\":[]}}}}",200);
    CHECK(cs_app.model.status==APP_EMPTY && cs_app.catalog.page->count==0);
    cs_action(APP_RETRY,0); cs_tick(cs_now+250); cs_response("bad json",200); CHECK(cs_app.model.status==APP_ERROR);
    cs_action(APP_RETRY,0); cs_tick(cs_now+250); cs_response("",401); CHECK(cs_app.model.status==APP_AUTH_EXPIRED && !cs_app.catalog.page);
    CHECK(app_session(&cs_app,true,true,cs_now)==APP_PENDING); cs_tick(cs_now+250); cs_fixture("search");
    const CatalogPage *late=cs_app.catalog.page; AppRequest late_request=cs_latest;
    CHECK(page_retain((JsonPage *)&late->json)); cs_query("replacement");
    CHECK(app_catalog_deliver(&cs_app,late_request,CATALOG_OK,late)==APP_STALE);
    cs_tick(cs_now+250); cs_fixture("search");
    cs_action(APP_CATALOG_SELECT,6); CHECK(cs_app.model.last_result==APP_UNAVAILABLE);
    cs_action(APP_CATALOG_SELECT,5); CHECK(cs_app.model.last_result==APP_OK && cs_command.track_id[15]==6);
    cs_action(APP_CATALOG_SECTION,SEARCH_ARTISTS); cs_tick(cs_now+250); cs_fixture("search");
    CHECK(cs_app.catalog.page->count==1 && cs_app.catalog.page->items[0].kind==CATALOG_ARTIST);
    (void)cs_layout(500,800,120); cs_app.widgets.focus=CLAY_ID("CatalogList").id;
    CHECK(app_key(&cs_app,APP_KEY_END,false)==APP_OK && app_key(&cs_app,APP_KEY_ENTER,false)==APP_OK);
    cs_tick(cs_now); cs_tick(cs_now); cs_fixture("artist");
    cs_action(APP_CATALOG_SECTION,ARTIST_SINGLES); cs_fixture("artist"); CHECK(cs_app.model.status==APP_EMPTY);
    cs_action(APP_CATALOG_SECTION,ARTIST_ALBUMS); cs_fixture("artist");
    cs_action(APP_CATALOG_SELECT,0); cs_fixture("album");
    AppEntity entity; CHECK(app_catalog_entity(&cs_app.catalog.page->detail,&entity)==APP_OK && strlen(entity.title)<192);
    CHECK(track_utf8(entity.title,strlen(entity.title)));
    cs_action(APP_MORE,0); cs_response("{\"data\":{\"albumUnion\":{\"__typename\":\"RestrictedContent\"}}}",200);
    CHECK(cs_app.catalog.result==CATALOG_RESTRICTED && cs_app.catalog.page && cs_app.model.status==APP_ERROR);
    cs_action(APP_RETRY,0); cs_fixture("album"); cs_action(APP_CATALOG_SELECT,0);
    cs_action(APP_GO_LYRICS,0); cs_response("",404);
    CHECK(cs_app.catalog.timeline.document->availability==LYRICS_MISSING && cs_app.catalog.lyric.deadline_ms==CORE_CLOCK_NEVER);
    const char *bodies[]={LYRICS("UNSYNCED","NONE"),LYRICS("LINE_SYNCED","PAYWALL"),LYRICS("LINE_SYNCED","NONE")};
    for (unsigned i=0;i<3;++i) {
        cs_action(APP_RETRY,0); cs_response(bodies[i],200);
        CHECK(cs_app.catalog.timeline.document);
        if (i<2) { CHECK(cs_app.catalog.lyric.line==LYRICS_NO_LINE && cs_app.catalog.lyric.deadline_ms==CORE_CLOCK_NEVER); }
        else { CHECK(cs_app.catalog.lyric.line==0 && cs_app.catalog.lyric.deadline_ms!=CORE_CLOCK_NEVER); }
    }
    AppRequest stale=cs_latest; const LyricsDocument *doc=cs_app.catalog.timeline.document;
    CHECK(lyrics_document_retain(doc));
    cs_player.track_id[15]=9; ++cs_player.generation; cs_tick(cs_now);
    CHECK(!cs_app.catalog.timeline.document && cs_app.model.status==APP_EMPTY);
    CHECK(app_lyrics_deliver(&cs_app,stale,LYRICS_OK,doc)==APP_STALE);
    CHECK(app_session(&cs_app,true,false,cs_now)==APP_UNAVAILABLE && cs_app.model.status==APP_OFFLINE);
    CHECK(app_session(&cs_app,false,false,cs_now)==APP_UNAVAILABLE && !cs_app.model.playing.title[0]);
    CHECK(!cs_app.model.playing_artwork[0]);
    CHECK(json_page_pool_resident(cs_host.pool)==0); cs_close();
}
static void partial_and_boundary(void)
{
    cs_open(false); query();
    cs_response("{\"errors\":[{\"message\":\"partial\"}],\"data\":{\"searchV2\":{\"topResultsV2\":{\"itemsV2\":[],\"pagingInfo\":{\"nextOffset\":0}}}}}",200);
    CHECK(cs_app.catalog.page && cs_app.catalog.page->partial && cs_app.catalog.page->boundary);
    (void)cs_layout(500,800,120); CHECK(cs_app.catalog.page->next==CATALOG_END);
    cs_action(APP_RETRY,0); cs_tick(cs_now+250); cs_response("",429);
    CHECK(cs_app.model.status==APP_ERROR && cs_app.catalog.result==CATALOG_RATE_LIMIT);
    cs_action(APP_RETRY,0); cs_tick(cs_now+250); cs_fixture("search");
    AppRequest previous=cs_latest;
    cs_action(APP_MORE,0); cs_response("",401);
    CHECK(cs_app.model.status==APP_AUTH_EXPIRED && !cs_app.catalog.page);
    CHECK(app_catalog_deliver(&cs_app,previous,CATALOG_ERROR,NULL)==APP_STALE);
    cs_close();
}
static void editing(void)
{
    cs_open(false); (void)cs_layout(1100,720,96); cs_app.widgets.focus=CLAY_ID("CatalogQuery").id;
    CHECK(app_search_text_message(&cs_app,NULL,WM_CHAR,'m',0)); cs_tick(10);
    CHECK(!strcmp(cs_app.model.location.query,"m") && cs_app.catalog.debounce==260);
    CHECK(app_search_text_message(&cs_app,NULL,WM_CHAR,0xd83c,0));
    CHECK(app_search_text_message(&cs_app,NULL,WM_CHAR,0xdfb5,0)); cs_tick(20);
    CHECK(cs_app.widgets.texts[0].length==5);
    CHECK(app_search_text_message(&cs_app,NULL,WM_KEYDOWN,VK_BACK,0)); cs_tick(30);
    CHECK(!strcmp(cs_app.model.location.query,"m"));
    CHECK(ui_text_set(&cs_app.widgets.texts[0],"",0)==UI_WIDGET_OK); cs_action(APP_CATALOG_QUERY,0);
    CHECK(cs_app.catalog.debounce==CORE_CLOCK_NEVER && cs_app.model.status==APP_EMPTY);
    cs_close();
}
static ArtResult image_failure(void *context, const wchar_t *url, void *buffer,
    size_t capacity, size_t *bytes, const CancelToken *cancel)
{
    (void)context; (void)buffer; (void)capacity; (void)cancel;
    CHECK(!wcscmp(url,L"https://fixture.invalid/cover.png")); *bytes=0; return ART_IO_ERROR;
}
static ArtResult no_upload(void *context, uint32_t width, uint32_t height, const unsigned char *rgba, UiTexture *out)
{
    (void)context; (void)width; (void)height; (void)rgba; (void)out; CHECK(false); return ART_INVALID;
}
static void no_release(void *context, UiTexture texture) { (void)context; (void)texture; CHECK(false); }
static bool no_texture(void *context, UiTexture texture) { (void)context; (void)texture; return false; }
static void detail_artwork(void)
{
    static _Alignas(16) unsigned char core_memory[MEMORY_BUDGET_CORE], storage[65536], decode[ART_DECODE_BYTES];
    static unsigned char upload[ART_UPLOAD_BYTES];
    Arena arena; Worker *worker; Artwork *art;
    cs_open(false);
    CHECK(arena_init(&arena,core_memory,sizeof(core_memory)));
    CHECK(worker_start(&arena,ART_REQUESTS,(CoreClock){0},&worker)==CORE_OK);
    ArtworkConfig config={.worker=worker,.fetch=image_failure,.gpu={no_upload,no_release,no_texture,NULL},
        .decode_workspace=decode,.upload=upload};
    CHECK(artwork_init(storage,sizeof(storage),&config,&art)==ART_OK); cs_app.services.artwork=art;
    CHECK(artwork_navigate(art,cs_app.model.generation)==ART_OK);
    query(); cs_fixture("search"); cs_action(APP_CATALOG_SELECT,1); cs_fixture("album");
    CHECK(cs_app.image_count>=1 && cs_app.images[0].id.size==160);
    CHECK(!strcmp(cs_app.images[0].entity.uri,cs_app.model.location.entity.uri));
    ArtworkId previous=cs_app.images[0].id;
    CHECK(previous.generation==cs_app.model.generation);
    cs_action(APP_MORE,0); cs_fixture("album-next");
    CHECK(cs_app.image_count>=1 && artwork_id_equal(cs_app.images[0].id,previous));
    cs_action(APP_GO_SETTINGS,0);
    CHECK(!cs_app.catalog.artwork[0] && !cs_app.image_count);
    ArtworkHandle handle; CHECK(!artwork_lookup(art,previous,&handle));
    ArtResult result=artwork_stop(art);
    for (unsigned i=0;result==ART_PENDING && i<50;++i) {
        (void)WaitForSingleObject(artwork_wait_handle(art),100); result=artwork_stop(art);
    }
    CHECK(result==ART_OK && worker_stop(worker)==CORE_OK && worker_join(worker,5000)==CORE_OK);
    arena_dispose(&arena); cs_app.services.artwork=NULL; cs_close();
}
void cs_faults(void) { responses(); partial_and_boundary(); editing(); detail_artwork(); }
