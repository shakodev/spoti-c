#include "app_shell_support.h"
#include "player_test.h"
#include "protocol/media_display.h"
#include <stdio.h>

static PlayerSnapshot snapshot;
static PlayerCommand last;
static unsigned submitted;
static PlayerResult result;
static PlayerSnapshot current(void *context) { (void)context; return snapshot; }
static PlayerResult submit(void *context,const PlayerCommand *command)
{
    (void)context; CHECK(!app.widgets.in_frame); last=*command; ++submitted; return result;
}
static bool info(void *context,const uint8_t id[16],MediaDisplay *out)
{
    (void)context; CHECK(!app.widgets.in_frame);
    if (id[15]!=2) { return false; }
    *out=(MediaDisplay){.title="Resolved next track",.artist="Resolved artist",.cover_id={0xab},.has_cover=true}; return true;
}
static bool queue(void *context,PlayerQueueSnapshot *out)
{
    (void)context; *out=(PlayerQueueSnapshot){.player=snapshot,.count=9,.index=1};
    for (unsigned i=0;i<9;++i) { out->ids[i][15]=(uint8_t)(i+1); } return true;
}
static const AppControl *control(const char *name)
{
    uint32_t id=Clay_GetElementId((Clay_String){false,(int32_t)strlen(name),name}).id;
    for (uint32_t i=0;i<app.control_count;++i) { if (app.controls[i].id.id==id) { return &app.controls[i]; } }
    CHECK(false); return NULL;
}
static Clay_BoundingBox box(const char *name)
{
    Clay_ElementData data=Clay_GetElementData(Clay_GetElementId((Clay_String){false,(int32_t)strlen(name),name}));
    CHECK(data.found); return data.boundingBox;
}
static void geometry(void)
{
    const float widths[]={400,500,759,760,1100,1600};
    for (unsigned dpi=96;dpi<=192;dpi+=48) {
        for (unsigned i=0;i<6;++i) {
            (void)shell_layout(widths[i]*(float)dpi/96,720.0f*(float)dpi/96,(float)dpi);
            Clay_BoundingBox bar=box("AppNowPlaying"), art=box("PlayerArtwork"), seek=box("AppSeek"), vol=box("AppVolume"), play=box("PlayerPlay");
            CHECK(art.width==(widths[i]<760 ? 40 : 56) && play.width==32 && seek.height==28);
            CHECK(art.x>=bar.x && vol.x+vol.width<=bar.x+bar.width+0.01f);
            CHECK(seek.y>=bar.y && vol.y+vol.height<=bar.y+bar.height+0.01f);
            CHECK(art.x+art.width<=play.x && seek.x+seek.width<=bar.x+bar.width);
            if (widths[i]>=760) { CHECK(fabsf(play.x+play.width/2-widths[i]/2)<0.1f); }
        }
    }
}
static void snapshots(void)
{
    shell_open(NULL); result=PLAYER_OK; submitted=0;
    app.services.submit=submit; app.services.snapshot=current; app.services.track_info=info; app.services.queue_snapshot=queue;
    snapshot=(PlayerSnapshot){.generation=1,.state=PLAYER_PLAYING,.volume=0.5f,.rate=48000,
        .duration_frames=480000,.position_frames=96000,.timeline_valid=true,.track_id={0}};
    snapshot.track_id[15]=1;
    CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    AppEntity track={.kind=COLLECTION_TRACK,.title="A very long title with mixed text: \xd7\x90\xd7\x91\xd7\x92 | artist",.playable=true}; track.id[15]=1;
    CHECK(app_track_remember(&app,&track,"First artist","https://fixture.invalid/first.png")==APP_OK);
    (void)app_update(&app,0); CHECK(!strcmp(app.model.playing.title,track.title)); geometry();
    shell_action(APP_SHUFFLE); CHECK(last.type==PLAYER_SHUFFLE && last.value==1 && !app.model.player.shuffled);
    snapshot.shuffled=true; (void)app_update(&app,1); shell_action(APP_SHUFFLE); CHECK(last.value==0);
    shell_action(APP_REPEAT); CHECK(last.type==PLAYER_REPEAT && last.value==PLAYER_REPEAT_ONE);
    snapshot.repeat=PLAYER_REPEAT_ONE; (void)app_update(&app,2); shell_action(APP_REPEAT); CHECK(last.value==PLAYER_REPEAT_ALL);
    snapshot.repeat=PLAYER_REPEAT_ALL; (void)app_update(&app,3); shell_action(APP_REPEAT); CHECK(last.value==PLAYER_REPEAT_OFF);
    result=PLAYER_FULL; shell_action(APP_REPEAT); CHECK(app.model.last_result==APP_FULL && app.model.player.repeat==PLAYER_REPEAT_ALL); result=PLAYER_OK;
    shell_action(APP_GO_SETTINGS); CHECK(!strcmp(app.model.playing.title,track.title));
    for (unsigned state=PLAYER_LOADING;state<=PLAYER_ERROR;++state) {
        snapshot.state=(PlayerState)state; (void)app_update(&app,4);
        (void)shell_layout(1100,720,96); CHECK(app.widgets.error==UI_WIDGET_OK);
    }
    snapshot.state=PLAYER_BUFFERING; snapshot.timeline_valid=false; (void)app_update(&app,5);
    (void)shell_layout(1100,720,96); CHECK(!strcmp(app.time,"0:02 / 0:10") && app.widgets.seeks[0].value==0.2);
    (void)app_update(&app,100000); (void)shell_layout(1100,720,96); CHECK(!strcmp(app.time,"0:02 / 0:10"));
    shell_action(APP_TOGGLE_PLAY); CHECK(last.type==PLAYER_PAUSE);
    snapshot.state=PLAYER_PAUSED; (void)app_update(&app,100001);
    CHECK(ui_seek_begin(&app.widgets.seeks[0],0.8)==UI_WIDGET_OK); app.widgets.active=CLAY_ID("AppSeek").id;
    ++snapshot.generation; snapshot.position_frames=48000; (void)app_update(&app,100002);
    CHECK(!app.widgets.seeks[0].dragging && app.widgets.active==0 && app.widgets.seeks[0].value==0.1);
    unsigned seeks_before=submitted;
    CHECK(app_enqueue(&app,(AppAction){.type=APP_SEEK,.value=0.8})==APP_OK);
    ++snapshot.generation; snapshot.position_frames=48000; (void)app_update(&app,100002);
    CHECK(submitted==seeks_before && app.model.last_result==APP_STALE);
    CHECK(ui_seek_begin(&app.widgets.seeks[0],0.9)==UI_WIDGET_OK); app.widgets.active=CLAY_ID("AppSeek").id;
    CHECK(app_layout(&app,1100,720,96,(UiWidgetInput){.cancel=true,.x=-10,.y=-10}).length>0);
    CHECK(app.widgets.seeks[0].value==0.1 && !app.action_count);
    snapshot.track_id[15]=2; ++snapshot.generation; snapshot.position_frames=0; (void)app_update(&app,100003);
    CHECK(!strcmp(app.model.playing.title,"Resolved next track") && !strcmp(app.model.playing_artist,"Resolved artist"));
    CHECK(!strcmp(app.model.playing.uri,"spotify:track:0000000000000000000002"));
    CHECK(!strcmp(app.model.playing_artwork,"https://i.scdn.co/image/ab00000000000000000000000000000000000000"));
    snapshot.track_id[15]=1; ++snapshot.generation; (void)app_update(&app,100004);
    CHECK(!strcmp(app.model.playing.title,track.title) && !strcmp(app.model.playing_artist,"First artist"));
    shell_action(APP_QUEUE); CHECK(app.now.queue_open && app.now.queue.count==9);
    (void)shell_layout(400,720,96); CHECK(Clay_GetElementData(CLAY_ID("PlayerQueuePanel")).found);
    app.widgets.focus=CLAY_ID("QueueLater").id; CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,100005);
    CHECK(app.now.queue_page==5); shell_action(APP_QUEUE); CHECK(!app.now.queue_open);
    (void)shell_layout(1600,900,96); const AppControl device=*control("PlayerDevice"); CHECK(device.action.type==APP_PLAYER_INFO);
    app.widgets.focus=device.id.id; (void)shell_layout(1600,900,96); CHECK(strstr(app.messages.text,"Connect device service"));
    unsigned before=submitted; CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK); (void)app_update(&app,100006);
    CHECK(submitted==before && strstr(app.model.notice,"Connect device service"));
    CHECK(app_session(&app,false,true,100007)==APP_UNAVAILABLE); (void)shell_layout(400,720,96);
    CHECK(!app.now.serial && !app.now.queue_open && !app.model.playing.title[0]); shell_close();
}
static void real_owner(void)
{
    shell_open(pt.player); CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    pt_track(PLAYER_LOAD,0,true); pt_track(PLAYER_QUEUE_APPEND,1,false); pt_drive(); (void)app_update(&app,0);
    shell_action(APP_REPEAT); pt_drive(); (void)app_update(&app,1); CHECK(app.model.player.repeat==PLAYER_REPEAT_ONE);
    shell_action(APP_SHUFFLE); pt_drive(); (void)app_update(&app,2); CHECK(app.model.player.shuffled);
    shell_action(APP_QUEUE); CHECK(app.now.queue.count==2 && app.now.queue.player.shuffled);
    CHECK(app.now.queue.ids[app.now.queue.index][15]==app.model.player.track_id[15]);
    shell_action(APP_NEXT); pt_drive(); (void)app_update(&app,3);
    CHECK(app.model.player.track_id[15]==2 && app.now.queue.index==1);
    shell_action(APP_PREVIOUS); pt_drive(); (void)app_update(&app,4); CHECK(app.model.player.track_id[15]==1);
    shell_close();
}
void shell_now_playing(void)
{
    snapshots(); real_owner();
    puts("now playing: centered geometry, metadata/queue transitions, modes, frozen clocks, external seek/cancel and unavailable reasons passed");
}
