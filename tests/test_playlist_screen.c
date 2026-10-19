#include "playlist_fixture.h"
#include "ui/app.h"
#include "ui/screens/playlist.h"
#include "player_test.h"
#include "support/ui_heap_watch.h"
#include <stdio.h>
#include <string.h>

static TrackDisk disk;
static wchar_t path[MAX_PATH];
static TrackFilter filter;
static Playlist playlist;
static App app;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES], core_memory[MEMORY_BUDGET_CORE];
static Arena core;
static Worker *worker;
static EventQueue *events;
static UiScheduler scheduler;
static unsigned requests,cancels,queues;
static PlaylistQueue copied;
static FontCache fonts;
static UiRenderFrame frame;
static AppResult request(void *context, const AppRequest *r)
{
    (void)context; CHECK(!app.widgets.in_frame && r->generation); ++requests; return APP_PENDING;
}
static void cancel(void *context, const AppRequest *r) { (void)context; CHECK(r->generation); ++cancels; }
static PlayerResult play(void *context, const PlaylistQueue *queue)
{
    (void)context; CHECK(!app.widgets.in_frame); copied=*queue; ++queues;
    PlayerTransfer transfer=queue->transfer; transfer.count=2;
    memset(transfer.ids,0,sizeof(transfer.ids)); transfer.ids[0][15]=transfer.ids[1][15]=1;
    PlayerResult result=player_submit(pt.player,&(PlayerCommand){.type=PLAYER_TRANSFER,.transfer=&transfer});
    memset(&transfer,0,sizeof(transfer)); return result;
}
static void layout(void)
{
    Clay_RenderCommandArray commands=app_layout(&app,1100,720,96,(UiWidgetInput){.x=-1,.y=-1});
    if (!commands.length || app.widgets.error!=UI_WIDGET_OK) {
        fprintf(stderr,"layout error=%d declarations=%u labels=%u count=%u ready=%u pending=%u generation=%llu\n",
            app.widgets.error,app.widgets.declarations,app.label_count,playlist.count,playlist.ready,playlist.pending,(unsigned long long)playlist.generation);
    }
    CHECK(commands.length>0 && app.widgets.error==UI_WIDGET_OK);
    CHECK(playlist.rendered_rows<=16 && app.widgets.declarations<120);
}
static void drain(void)
{
    Event batch[32]; size_t n; CoreResult r=event_drain(events,batch,32,&n); CHECK(r==CORE_OK || r==CORE_CLOSED);
}
static void settle(void)
{
    for (unsigned i=0;i<200;++i) {
        (void)app_update(&app,GetTickCount64()); layout();
        if (!playlist.pending && atomic_load_explicit(&filter.phase,memory_order_acquire)==0) { return; }
        (void)WaitForSingleObject(event_wait_handle(events),50); drain();
    }
    CHECK(false);
}
static void action(AppActionType type, double value)
{
    CHECK(app_enqueue(&app,(AppAction){.type=type,.value=value})==APP_OK); (void)app_update(&app,0);
}
static void query(const char *text, TrackSort sort)
{
    CHECK(ui_text_set(&app.widgets.texts[0],text,(uint32_t)strlen(text))==UI_WIDGET_OK);
    action(APP_TRACK_SORT,(double)sort); settle();
}
static TrackRecord sparse[2];
static TrackRecord ordered_rows[8];
static TrackResult read_ordered(void *context, uint32_t first, uint32_t count, TrackRecord *out)
{
    (void)context; CHECK(first<=8 && count<=8-first); memcpy(out,ordered_rows+first,(size_t)count*sizeof(*out)); return TRACK_OK;
}
static TrackResult read_sparse(void *context, uint32_t first, uint32_t count, TrackRecord *out)
{
    (void)context; CHECK(first<=2 && count<=2-first); memcpy(out,sparse+first,(size_t)count*sizeof(*out)); return TRACK_OK;
}
static TrackResult read_failed(void *context, uint32_t first, uint32_t count, TrackRecord *out)
{
    (void)context; (void)first; (void)count; (void)out; return TRACK_IO;
}
static void filters_and_identity(void)
{
    CHECK(playlist.count==10000 && playlist.coverage==TRACK_FULL_INDEX && playlist.row_count<=16);
    CHECK(sizeof(Playlist)+sizeof(TrackFilter)+COLLECTION_MODEL_BUDGET<=512u*1024u);
    CHECK(playlist_select(&playlist,2,false)==TRACK_OK);
    CHECK(playlist_select(&playlist,3,false)==TRACK_OK); /* Same song ID, distinct occurrence. */
    PlaylistQueue a,b;
    CHECK(playlist_queue(&playlist,3,&a)==TRACK_OK && a.ordinals[0]==2 && a.more);
    CHECK(playlist_queue(&playlist,4,&b)==TRACK_OK && b.ordinals[0]==3);
    CHECK(!memcmp(a.transfer.ids[0],b.transfer.ids[0],16));
    CHECK(playlist_queue(&playlist,1,&a)==TRACK_INVALID); /* Unavailable first row. */
    query("sOnG 000",TRACK_SOURCE_ORDER); CHECK(playlist.count==100 && playlist.coverage==TRACK_FULL_INDEX);
    CHECK(playlist.selected==3);
    query("Artist 16",TRACK_SOURCE_ORDER); CHECK(playlist.count>500 && playlist.selected==3);
    query("\xe6\x97\xa5\xe6\x9c\xac",TRACK_SOURCE_ORDER); CHECK(playlist.count==1 && playlist.map[0]==777);
    query("Bj\xc3\xb6rk",TRACK_SOURCE_ORDER); CHECK(playlist.count==1);
    query("BJ\xc3\x96RK",TRACK_SOURCE_ORDER); CHECK(playlist.count==0); /* Only ASCII folds. */
    query("Song 000",TRACK_TITLE_ORDER);
    for (uint32_t i=1;i<playlist.count;++i) { CHECK(playlist.map[i-1]<playlist.map[i]); }
    query("",TRACK_DURATION_ORDER);
    for (uint32_t i=1;i<playlist.count;++i) {
        uint32_t x=playlist.map[i-1],y=playlist.map[i];
        uint32_t u=filter.index[x].duration_ms,v=filter.index[y].duration_ms;
        CHECK(u<v || (u==v && x<y));
    }
    CHECK(playlist.selected==3); query("",TRACK_SOURCE_ORDER);
    uint64_t generation=playlist.generation; char oversized[TRACK_QUERY_BYTES+1]; memset(oversized,'x',sizeof(oversized)-1); oversized[sizeof(oversized)-1]=0;
    CHECK(playlist_query(&playlist,oversized,TRACK_SOURCE_ORDER)==TRACK_FULL && playlist.generation==generation);
    CHECK(playlist_query(&playlist,"\xc0\x80",TRACK_SOURCE_ORDER)==TRACK_INVALID && playlist.generation==generation);
    const float widths[]={400,760,1100,1440}, dpis[]={96,120,192};
    for (unsigned i=0;i<4;++i) { for (unsigned j=0;j<3;++j) {
        CHECK(app_layout(&app,widths[i]*dpis[j]/96,720*dpis[j]/96,dpis[j],(UiWidgetInput){0}).length>0);
        CHECK(app.widgets.error==UI_WIDGET_OK && playlist.rendered_rows<=16);
    } }
    layout();
}
static void scroll_keyboard_play(void)
{
    app.widgets.lists[0].scroll=40.0*5000; settle(); layout();
    CHECK(playlist_row(&playlist,5000) && !playlist_row(&playlist,0));
    CHECK(playlist_select(&playlist,5001,false)==TRACK_OK);
    app.widgets.focus=CLAY_ID("PlaylistList").id; app.widgets.lists[0].focus=5001;
    CHECK(app_key(&app,APP_KEY_DOWN,false)==APP_OK && playlist.selected==5002);
    CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,0);
    CHECK(queues==1 && copied.ordinals[0]==5001 && copied.transfer.count==PLAYER_QUEUE_CAP);
    CHECK(!strcmp(copied.revision,"fixture-revision-1"));
    /* The real player copies a transfer. Fake-source IDs are remapped to fixture 1. */
    pt_drive();
    CHECK(player_snapshot(pt.player).state==PLAYER_PLAYING);
    (void)app_update(&app,0);
    action(APP_TOGGLE_PLAY,0); pt_drive(); (void)app_update(&app,0);
    CHECK(app.model.player.state==PLAYER_PAUSED);
    CHECK(app_key(&app,APP_KEY_END,false)==APP_OK); settle();
    CHECK(playlist.selected==10000 && playlist_row(&playlist,9999));
    app.widgets.focus=CLAY_ID("PlaylistFilter").id;
    CHECK(playlist_text_message(&app,NULL,WM_CHAR,'S',0));
    (void)app_update(&app,0); settle(); CHECK(!strcmp(playlist.query,"S"));
    CHECK(playlist_text_message(&app,NULL,WM_CHAR,0xd83c,0));
    CHECK(playlist_text_message(&app,NULL,WM_CHAR,0xdfb5,0)); (void)app_update(&app,0); settle();
    CHECK(app.widgets.texts[0].length==5 && playlist.count==0);
    query("",TRACK_SOURCE_ORDER); layout();
    ui_heap_begin(); for (unsigned i=0;i<2000;++i) { layout(); } ui_heap_end();
}
static HANDLE blocked,unblock;
static TrackResult read_blocked(void *context, uint32_t first, uint32_t count, TrackRecord *out)
{
    CHECK(SetEvent(blocked)); CHECK(WaitForSingleObject(unblock,5000)==WAIT_OBJECT_0);
    TrackSource *source=context; return source->read(source->context,first,count,out);
}
static void generations_and_coverage(void)
{
    blocked=CreateEventW(NULL,TRUE,FALSE,NULL); unblock=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(blocked && unblock);
    TrackSource source=disk.source; source.read=read_blocked; source.context=&disk.source;
    CHECK(playlist_source(&playlist,&source,app.model.generation)==TRACK_PENDING);
    (void)app_update(&app,0); CHECK(WaitForSingleObject(blocked,5000)==WAIT_OBJECT_0);
    CHECK(playlist_query(&playlist,"Song 099",TRACK_SOURCE_ORDER)==TRACK_PENDING);
    CHECK(playlist_query(&playlist,"Song 009",TRACK_SOURCE_ORDER)==TRACK_PENDING);
    CHECK(SetEvent(unblock)); settle(); CHECK(playlist.stale_jobs>0 && playlist.count==100);
    CHECK(CloseHandle(blocked) && CloseHandle(unblock));
    CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING); query("",TRACK_SOURCE_ORDER);
    settle(); CHECK(playlist_select(&playlist,3,false)==TRACK_OK);
    source=disk.source; strcpy_s(source.revision,sizeof(source.revision),"fixture-revision-2");
    CHECK(playlist_source(&playlist,&source,app.model.generation)==TRACK_PENDING && !playlist.selected); settle();
    CHECK(playlist_source(&playlist,&disk.source,app.model.generation-1)==TRACK_STALE);
    playlist_fixture_record(0,&sparse[0]); playlist_fixture_record(1,&sparse[1]); sparse[1].flags&=~(uint32_t)TRACK_METADATA;
    source=(TrackSource){.read=read_sparse,.total=2,.complete=true,.uri="spotify:playlist:fixture",.revision="sparse"};
    CHECK(playlist_source(&playlist,&source,app.model.generation)==TRACK_PENDING); query("Song",TRACK_SOURCE_ORDER);
    CHECK(playlist.count==1 && playlist.coverage==TRACK_LOADED_ONLY && playlist.missing==1);
    source.complete=false; CHECK(playlist_source(&playlist,&source,app.model.generation)==TRACK_PENDING);
    query("",TRACK_SOURCE_ORDER); CHECK(playlist.count==2 && playlist.coverage==TRACK_LOADED_ONLY);
    const char *titles[]={"z","Alpha","alpha","Zebra","Bj\xc3\xb6rk","beta","\xe3\x81\x82",""};
    const uint32_t expected[]={7,1,2,5,4,0,3,6};
    for (unsigned i=0;i<8;++i) {
        playlist_fixture_record(i,&ordered_rows[i]); strcpy_s(ordered_rows[i].title,96,titles[i]);
        ordered_rows[i].artist[0]=ordered_rows[i].album[0]=0;
    }
    source=(TrackSource){.read=read_ordered,.total=8,.complete=true,.uri="spotify:playlist:fixture",.revision="sort-test"};
    CHECK(playlist_source(&playlist,&source,app.model.generation)==TRACK_PENDING); query("",TRACK_TITLE_ORDER);
    CHECK(playlist.count==8 && !memcmp(playlist.map,expected,sizeof(expected)));
    query("AL",TRACK_TITLE_ORDER); CHECK(playlist.count==2 && playlist.map[0]==1 && playlist.map[1]==2);
    query("",TRACK_SOURCE_ORDER);
    TrackDisk larger; wchar_t larger_path[MAX_PATH]; playlist_fixture_disk(&larger,larger_path,10005);
    CHECK(playlist_source(&playlist,&larger.source,app.model.generation)==TRACK_PENDING); settle();
    CHECK(playlist.count==10000 && playlist.coverage==TRACK_WINDOW);
    action(APP_TRACK_WINDOW,1); settle(); CHECK(playlist.count==5 && playlist.base==10000 && playlist.map[0]==10000);
    action(APP_TRACK_WINDOW,-1); settle(); CHECK(playlist.count==10000 && playlist.base==0);
    CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING); settle();
    CHECK(track_disk_close(&larger) && DeleteFileW(larger_path));
    source=disk.source; source.read=read_failed;
    CHECK(playlist_source(&playlist,&source,app.model.generation)==TRACK_PENDING); settle();
    CHECK(playlist.result==TRACK_IO && app.model.status==APP_ERROR && !playlist.ready);
    uint64_t jobs=worker_stats(worker).accepted;
    for (unsigned i=0;i<20;++i) { (void)app_update(&app,0); }
    CHECK(worker_stats(worker).accepted==jobs); /* No automatic error retry/wake loop. */
    CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING); settle();
}
static void font_frames(void)
{
    CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    FontMeasureContext text={.cache=&fonts,.config={.dpi=96,.weight=400,.max_width=260,.ellipsis=true}};
    app_bind_fonts(&app,&text); UiClayBridge bridge={.custom=app_render_custom,.context=&app};
    for (unsigned i=0;i<80;++i) {
        CHECK(font_prepare_pending(&fonts)==FONT_OK && font_atlas_commit(&fonts,1)==FONT_OK);
        layout(); CHECK(ui_frame_begin(&frame,1100,720,1)==UI_RENDER_OK);
        CHECK(ui_frame_clay(&frame,app.commands,&bridge)==UI_RENDER_OK);
    }
    CHECK(font_stats(&fonts).pending==0); uint64_t prepared=font_stats(&fonts).prepared;
    ui_heap_begin();
    for (unsigned i=0;i<1000;++i) {
        layout(); CHECK(ui_frame_begin(&frame,1100,720,1)==UI_RENDER_OK);
        CHECK(ui_frame_clay(&frame,app.commands,&bridge)==UI_RENDER_OK);
        CHECK(font_stats(&fonts).pending==0 && font_stats(&fonts).prepared==prepared);
    }
    ui_heap_end(); font_cache_destroy(&fonts); app.fonts=NULL;
    puts("playlist: 1000 real DirectWrite/Clay/render warm frames without allocations, pending text or new shaping");
}
static DWORD WINAPI run(void *context)
{
    bool visual=*(bool *)context; DWORD before,after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    pt_load_fixtures(); pt_open(); playlist_fixture_disk(&disk,path,10000);
    CHECK(track_disk_close(&disk) && track_disk_open(&disk,path)==TRACK_OK);
    TrackRecord invalid={.flags=TRACK_PLAYABLE}; CHECK(!track_record_valid(&invalid));
    memset(invalid.title,'x',sizeof(invalid.title)); CHECK(!track_record_valid(&invalid));
    CHECK(arena_init(&core,core_memory,sizeof(core_memory)) && event_queue_create(&core,32,&events)==CORE_OK);
    CHECK(worker_start(&core,4,(CoreClock){0},&worker)==CORE_OK);
    CHECK(track_filter_init(&filter,worker,events)==TRACK_OK); playlist_init(&playlist,&filter);
    ui_scheduler_init(&scheduler);
    AppServices services={.request=request,.cancel=cancel,.playlist=&playlist,.playlist_queue=play,.player=pt.player};
    CHECK(app_init(&app,&services,&scheduler,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    CHECK(app_session(&app,true,true,0)==APP_PENDING);
    AppEntity entity={.kind=COLLECTION_PLAYLIST_ITEM,.uri="spotify:playlist:fixture",.title="Fixture playlist"};
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SELECT,.entity=entity})==APP_OK); (void)app_update(&app,0);
    CHECK(app.model.location.screen==APP_PLAYLIST && cancels==1 && requests==2);
    CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING); layout(); settle();
    filters_and_identity(); scroll_keyboard_play(); generations_and_coverage();
    for (unsigned i=0;i<25;++i) {
        action(APP_GO_HOME,0); CHECK(!playlist.active);
        CHECK(app_enqueue(&app,(AppAction){.type=APP_SELECT,.entity=entity})==APP_OK); (void)app_update(&app,0);
        CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING); settle();
        CHECK(playlist.count==10000 && playlist.row_count<=16);
        action(APP_BACK,0); CHECK(!playlist.active);
        action(APP_BACK,0); /* Return to previous playlist without growing history. */
        CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING); settle();
    }
    CHECK(playlist_query(&playlist,"Song",TRACK_SOURCE_ORDER)==TRACK_PENDING); (void)app_update(&app,0);
    action(APP_GO_HOME,0); CHECK(!playlist.active); settle();
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SELECT,.entity=entity})==APP_OK); (void)app_update(&app,0);
    CHECK(playlist_source(&playlist,&disk.source,app.model.generation)==TRACK_PENDING); settle();
    pt_drive(); pt_close(); app.services.player=NULL;
    CHECK(worker_stop(worker)==CORE_OK && worker_join(worker,5000)==CORE_OK);
    CHECK(track_disk_close(&disk) && DeleteFileW(path));
    CHECK(event_queue_close(events)==CORE_OK); drain(); CHECK(event_queue_destroy(events)==CORE_OK); arena_dispose(&core);
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    /* The visible copied page remains renderable after its disk source and
     * workers close. DirectWrite may keep OS-global caches after Release. */
    font_frames(); int result=visual ? playlist_visual(&app) : 0; app_dispose(&app);
    printf("app_playlist: 10k index, <=16 rendered rows, 2000 warm layouts without heap calls; shared models=%zu bytes\n",
        sizeof(Playlist)+sizeof(TrackFilter)+COLLECTION_MODEL_BUDGET);
    return (DWORD)result;
}
int main(int argc, char **argv)
{
    bool visual=argc==2 && !strcmp(argv[1],"--visual");
    HANDLE thread=CreateThread(NULL,0,run,&visual,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread,110000)==WAIT_OBJECT_0); DWORD code;
    CHECK(GetExitCodeThread(thread,&code) && CloseHandle(thread)); return (int)code;
}
