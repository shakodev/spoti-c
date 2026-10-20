#include "app_shell_support.h"
#include "ui/startup.h"
#include "player_test.h"
#include "support/ui_heap_watch.h"
#include <math.h>

static PlayerSnapshot snapshot;
static PlayerCommand volume_commands[256];
static unsigned submitted;
static PlayerResult submit_result;
static PlayerResult submit(void *context,const PlayerCommand *command)
{
    (void)context; CHECK(!app.widgets.in_frame);
    if (submit_result==PLAYER_OK) { CHECK(submitted<256); volume_commands[submitted++]=*command; }
    return submit_result;
}
static PlayerSnapshot current(void *context) { (void)context; return snapshot; }
static void level_is_at(double value,double expected,int line)
{
    if (fabs(value-expected)>=0.0001) { fprintf(stderr,"volume at line %d: %.9f expected %.9f\n",line,value,expected); }
    CHECK(fabs(value-expected)<0.0001);
}
#define level_is(value,expected) level_is_at(value,expected,__LINE__)
static void volume(double value)
{
    CHECK(app_enqueue(&app,(AppAction){.type=APP_VOLUME,.value=value})==APP_OK);
    (void)app_update(&app,100);
}
static void acknowledge(float level)
{
    snapshot.volume=level; snapshot.volume_ticket=0;
    for (unsigned i=submitted;i;--i) {
        if (volume_commands[i-1].type==PLAYER_VOLUME && fabsf(volume_commands[i-1].volume-level)<0.0001f) {
            snapshot.volume_ticket=volume_commands[i-1].ticket; break;
        }
    }
    (void)app_update(&app,100);
}
static void pointer(float x,float y,bool down,bool pressed,bool released,bool cancel)
{
    CHECK(app_layout(&app,1100,720,96,(UiWidgetInput){.x=x,.y=y,.down=down,
        .pressed=pressed,.released=released,.cancel=cancel}).length>0);
    (void)app_update(&app,100);
}
static const AppControl *control(const char *name)
{
    uint32_t id=Clay_GetElementId((Clay_String){false,(int32_t)strlen(name),name}).id;
    for (uint32_t i=0;i<app.control_count;++i) { if (app.controls[i].id.id==id) { return &app.controls[i]; } }
    CHECK(false); return NULL;
}
static bool text_is(Clay_RenderCommandArray commands,const char *text)
{
    size_t n=strlen(text);
    for (int32_t i=0;i<commands.length;++i) {
        Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT && c->renderData.text.stringContents.length==(int32_t)n &&
            !memcmp(c->renderData.text.stringContents.chars,text,n)) { return true; }
    }
    return false;
}
static void geometry(void)
{
    const float widths[]={400,500,759,760,1100,1440};
    for (unsigned d=0;d<4;++d) {
        float dpi=96+32.0f*d;
        for (unsigned i=0;i<6;++i) {
            (void)shell_layout(widths[i]*dpi/96,640*dpi/96,dpi);
            Clay_ElementData bar=Clay_GetElementData(CLAY_ID("AppNowPlaying"));
            Clay_ElementData slider=Clay_GetElementData(CLAY_ID("AppVolume"));
            Clay_ElementData mute=Clay_GetElementData(CLAY_ID("PlayerMute"));
            Clay_ElementData transport=Clay_GetElementData(CLAY_ID("PlayerLyrics"));
            CHECK(bar.found && slider.found && mute.found && transport.found);
            CHECK(slider.boundingBox.width>=112 && slider.boundingBox.height==28);
            CHECK(mute.boundingBox.x>=bar.boundingBox.x && slider.boundingBox.x+slider.boundingBox.width<=bar.boundingBox.x+bar.boundingBox.width);
            CHECK(slider.boundingBox.y>=bar.boundingBox.y && slider.boundingBox.y+28<=bar.boundingBox.y+bar.boundingBox.height);
            CHECK(mute.boundingBox.x>=transport.boundingBox.x+transport.boundingBox.width);
        }
    }
}
static void fake_state(void)
{
    shell_open(NULL); submitted=0; submit_result=PLAYER_OK; snapshot=(PlayerSnapshot){.volume=1};
    level_is(app.model.volume.value,1); level_is(app.model.volume.nonzero,1);
    (void)shell_layout(1100,720,96); CHECK(control("AppVolume")->disabled && control("PlayerMute")->action.type==APP_PLAYER_INFO);
    volume(0.2); CHECK(app.model.last_result==APP_UNAVAILABLE); level_is(app.model.volume.value,1);
    CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    volume(0.2); CHECK(app.model.last_result==APP_UNAVAILABLE);
    app.services.submit=submit; app.services.snapshot=current; (void)app_update(&app,100);
    geometry(); (void)shell_layout(1100,720,96);
    CHECK(!control("AppVolume")->disabled && !control("PlayerMute")->disabled);
    CHECK(app_enqueue(&app,(AppAction){.type=APP_VOLUME,.value=NAN})==APP_INVALID);
    volume(-5); level_is(volume_commands[submitted-1].volume,0); level_is(app.model.volume.value,0);
    shell_action(APP_MUTE); level_is(volume_commands[submitted-1].volume,1);
    acknowledge(0); level_is(app.model.volume.value,1); acknowledge(1); CHECK(!app.model.volume.pending_ticket);
    volume(0.37); acknowledge(0.37f); shell_action(APP_MUTE); acknowledge(0);
    level_is(app.model.volume.nonzero,0.37); shell_action(APP_MUTE); level_is(volume_commands[submitted-1].volume,0.37);
    acknowledge(0.37f); volume(5); level_is(volume_commands[submitted-1].volume,1); acknowledge(1);
    volume(0.25); volume(0.65); acknowledge(0.25f); level_is(app.model.volume.value,0.65);
    acknowledge(0.25f); level_is(app.model.volume.value,0.65); acknowledge(0.65f); CHECK(!app.model.volume.pending_ticket);
    unsigned before=submitted;
    for (unsigned i=0;i<200;++i) { (void)app_update(&app,100); }
    CHECK(submitted==before);
    acknowledge(0.66f); level_is(app.model.volume.value,0.66); level_is(app.widgets.seeks[1].value,0.66);
    shell_action(APP_GO_SETTINGS); Clay_RenderCommandArray layout=shell_layout(1100,720,96);
    CHECK(text_is(layout,"66%")); CHECK(submitted==before);
    app.widgets.focus=CLAY_ID("VolumeUp").id; CHECK(app_key(&app,APP_KEY_ENTER,false)==APP_OK);
    (void)app_update(&app,100); level_is(volume_commands[submitted-1].volume,0.76); acknowledge(0.76f);
    app.widgets.focus=CLAY_ID("AppVolume").id;
    CHECK(app_key(&app,APP_KEY_HOME,false)==APP_OK); (void)app_update(&app,100); acknowledge(0);
    CHECK(app_key(&app,APP_KEY_END,false)==APP_OK); (void)app_update(&app,100); acknowledge(1);
    CHECK(app_key(&app,APP_KEY_LEFT,false)==APP_OK); (void)app_update(&app,100); acknowledge(0.99f);
    level_is(volume_commands[submitted-1].volume,0.99);
    CHECK(app_key(&app,APP_KEY_UP,false)==APP_OK); (void)app_update(&app,100); acknowledge(1);
    CHECK(app_key(&app,APP_KEY_RIGHT,false)==APP_OK); before=submitted; (void)app_update(&app,100); CHECK(submitted==before);
    app.widgets.focus=CLAY_ID("PlayerMute").id;
    CHECK(app_key(&app,APP_KEY_TAB,false)==APP_OK && app.widgets.focus==CLAY_ID("AppVolume").id);
    (void)shell_layout(1100,720,96);
    Clay_BoundingBox box=Clay_GetElementData(CLAY_ID("AppVolume")).boundingBox;
    pointer(box.x+box.width*0.25f,box.y+14,true,true,false,false);
    level_is(volume_commands[submitted-1].volume,0.25); CHECK(app.widgets.seeks[1].dragging);
    pointer(box.x+box.width*0.75f,box.y+14,true,false,false,false);
    level_is(volume_commands[submitted-1].volume,0.75); acknowledge(0.25f); level_is(app.model.volume.value,0.75);
    pointer(box.x+box.width+50,box.y+100,false,false,true,false);
    level_is(volume_commands[submitted-1].volume,1); CHECK(!app.widgets.seeks[1].dragging); acknowledge(1);
    pointer(box.x+box.width*0.4f,box.y+14,true,true,false,false); acknowledge(0.4f);
    before=submitted; pointer(0,0,false,false,false,true); CHECK(submitted==before); level_is(app.widgets.seeks[1].value,0.4);
    pointer(box.x+box.width*0.8f,box.y+14,true,true,false,false);
    acknowledge(0.53f); level_is(app.model.volume.value,0.53); CHECK(!app.widgets.seeks[1].dragging && !app.model.volume.pending_ticket);
    before=submitted; pointer(box.x+box.width,box.y+14,true,false,false,false); CHECK(submitted==before);
    acknowledge(0); shell_action(APP_MUTE); level_is(volume_commands[submitted-1].volume,0.53); acknowledge(0.53f);
    submit_result=PLAYER_FULL; before=submitted; volume(0.12);
    CHECK(app.model.last_result==APP_FULL && submitted==before); level_is(app.model.volume.value,0.53);
    submit_result=PLAYER_OK;
    for (unsigned i=0;i<APP_ACTIONS;++i) { volume((double)(i+1)/100); }
    CHECK(app.model.volume.pending_ticket); before=submitted;
    uint64_t serial=app.model.volume.serial; app.model.volume.serial=UINT64_MAX;
    volume(0.9); CHECK(app.model.last_result==APP_FULL && submitted==before); app.model.volume.serial=serial;
    acknowledge(0.32f); CHECK(!app.model.volume.pending_ticket); volume(0.7); acknowledge(0.7f);
    for (unsigned i=0;i<100;++i) { CHECK(app_enqueue(&app,(AppAction){.type=APP_VOLUME,.value=i/100.0})==APP_OK); }
    CHECK(app.action_count==1); before=submitted; (void)app_update(&app,100); CHECK(submitted==before+1); acknowledge(0.99f);
    shell_action(APP_GO_HOME); shell_deliver("home"); level_is(app.model.volume.value,0.99);
    ui_heap_begin(); for (unsigned i=0;i<2000;++i) { (void)shell_layout(400,640,96); } ui_heap_end();
    CHECK(app_session(&app,false,false,100)==APP_UNAVAILABLE); before=submitted;
    volume(0.1); CHECK(app.model.last_result==APP_UNAVAILABLE && submitted==before); level_is(app.model.volume.value,0.99);
    shell_close();
}
static void persistence(void)
{
    static StateCache state,decoded; static unsigned char wire[STATE_CACHE_WIRE_MAX]; size_t bytes;
    shell_open(NULL); state_cache_defaults(&state,STARTUP_EPOCH); state.volume=0; state.volume_nonzero=420;
    startup_restore(&app,&state); level_is(app.model.volume.value,0); level_is(app.model.volume.nonzero,0.42);
    app.services.submit=submit; app.services.snapshot=current; snapshot=(PlayerSnapshot){.volume=1}; submitted=0;
    CHECK(app_session(&app,true,true,100)==APP_PENDING); shell_deliver("home");
    submit_result=PLAYER_FULL; (void)app_update(&app,100); CHECK(app.model.volume.restore_pending && !submitted);
    level_is(app.model.volume.value,0); submit_result=PLAYER_OK; (void)app_update(&app,100);
    CHECK(submitted==1 && volume_commands[0].type==PLAYER_VOLUME && volume_commands[0].volume==0);
    (void)app_update(&app,100); CHECK(submitted==1); acknowledge(0);
    shell_action(APP_MUTE); level_is(volume_commands[submitted-1].volume,0.42); acknowledge(0.42f);
    shell_action(APP_MUTE); /* Capture the intended mute even before its acknowledgement. */
    startup_capture(&decoded,&app,NULL); CHECK(decoded.volume==0 && decoded.volume_nonzero==420);
    CHECK(state_cache_encode(&decoded,wire,sizeof(wire),&bytes)==STATE_CACHE_OK);
    CHECK(state_cache_decode(wire,bytes,STARTUP_EPOCH,&state)==STATE_CACHE_OK);
    shell_close(); shell_open(NULL); startup_restore(&app,&state);
    level_is(app.model.volume.value,0); level_is(app.model.volume.nonzero,0.42);
    (void)app_session(&app,false,false,100); startup_capture(&decoded,&app,NULL);
    CHECK(decoded.volume==0 && decoded.volume_nonzero==420); shell_close();
}
void shell_volume(void)
{
    fake_state(); persistence();
    shell_open(pt.player); CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    volume(0.31); pt_drive(); (void)app_update(&app,100); level_is(app.model.player.volume,0.31);
    shell_action(APP_MUTE); pt_drive(); (void)app_update(&app,100); level_is(player_snapshot(pt.player).volume,0);
    shell_action(APP_MUTE); pt_drive(); (void)app_update(&app,100); level_is(player_snapshot(pt.player).volume,0.31);
    PlayerCommand external={.type=PLAYER_VOLUME,.volume=0.62f};
    CHECK(player_submit(pt.player,&external)==PLAYER_OK); pt_drive(); (void)app_update(&app,100);
    level_is(app.model.volume.value,0.62); level_is(app.widgets.seeks[1].value,0.62); shell_close();
    puts("volume: continuous input, mute, snapshot ordering/backpressure, Settings, compact layouts, restart and real player passed");
}
