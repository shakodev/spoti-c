/* Separate opt-in Release/account/WASAPI check. Never part of default CTest. */
#include "ui/app.h"
#include "headless/headless.h"
#include "protocol/media_source.h"
#include <stdio.h>
#include <string.h>

static App app;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static HANDLE changed;
static PlayerSnapshot snapshot(void *h) { return headless_snapshot(h); }
static PlayerResult submit(void *h,const PlayerCommand *c) { return headless_submit(h,c); }
static bool info(void *h,const uint8_t id[16],MediaDisplay *out) { return headless_track_info(h,id,out); }
static bool queue(void *h,PlayerQueueSnapshot *out) { return headless_queue_snapshot(h,out); }
static void wake(void *context) { (void)SetEvent(context); }
static bool wait_state(Headless *h,PlayerState state,const uint8_t id[16],uint64_t minimum)
{
    uint64_t until=GetTickCount64()+30000, next=0;
    while (GetTickCount64()<until) {
        uint64_t now=GetTickCount64();
        if (now>=next) { (void)headless_refresh_clock(h); next=now+1000; }
        (void)app_update(&app,now); PlayerSnapshot p=app.model.player;
        if (p.state==PLAYER_ERROR) { fprintf(stderr,"Playback state error: %u\n",(unsigned)p.error); return false; }
        if (p.state==state && (!id || !memcmp(p.track_id,id,16)) && p.position_frames>=minimum) { return true; }
        (void)WaitForSingleObject(changed,100);
    }
    fputs("Timed out waiting for the authoritative playback snapshot\n",stderr); return false;
}
static bool action(AppActionType type,double value)
{
    if (app_enqueue(&app,(AppAction){.type=type,.value=value})!=APP_OK) { return false; }
    (void)app_update(&app,GetTickCount64()); return app.model.last_result==APP_OK;
}
static bool wait_volume(float level)
{
    uint64_t until=GetTickCount64()+5000;
    do {
        (void)app_update(&app,GetTickCount64());
        if (!app.model.volume.pending_ticket && app.model.player.volume==level) { return true; }
        (void)WaitForSingleObject(changed,100);
    } while (GetTickCount64()<until);
    return false;
}
static bool controls(Headless *h,const uint8_t ids[2][16])
{
    PlayerTransfer transfer={.count=2,.play=true}; memcpy(transfer.ids,ids,32);
    PlayerCommand c={.type=PLAYER_TRANSFER,.transfer=&transfer};
    if (headless_submit(h,&c)!=PLAYER_OK || !wait_state(h,PLAYER_PLAYING,ids[0],96000)) { return false; }
    if (!app.model.playing.title[0] || !app.model.playing_artist[0] || !app.model.playing_artwork[0] ||
        memcmp(app.model.playing.id,ids[0],16)) { fputs("Resolved display metadata/cover missing\n",stderr); return false; }
    if (!action(APP_TOGGLE_PLAY,0) || !wait_state(h,PLAYER_PAUSED,ids[0],0)) { return false; }
    uint64_t frozen=app.model.player.position_frames;
    (void)WaitForSingleObject(changed,500); (void)headless_refresh_clock(h);
    (void)WaitForSingleObject(changed,500); (void)app_update(&app,GetTickCount64());
    if (app.model.player.position_frames!=frozen) { return false; }
    uint64_t generation=app.model.player.generation, target=(uint64_t)((double)app.model.player.duration_frames*0.05);
    if (!action(APP_SEEK,0.05)) { return false; }
    uint64_t until=GetTickCount64()+5000;
    do { (void)app_update(&app,GetTickCount64()); (void)WaitForSingleObject(changed,100); }
    while (app.model.player.generation==generation && GetTickCount64()<until);
    if (app.model.player.generation==generation || app.model.player.state!=PLAYER_PAUSED || app.model.player.position_frames!=target) { return false; }
    if (!action(APP_VOLUME,0.25) || !wait_volume(0.25f) || !action(APP_MUTE,0) || !wait_volume(0) ||
        !action(APP_MUTE,0) || !wait_volume(0.25f)) { return false; }
    if (!action(APP_TOGGLE_PLAY,0) || !wait_state(h,PLAYER_PLAYING,ids[0],0)) { return false; }
    if (!action(APP_NEXT,0) || !wait_state(h,PLAYER_PLAYING,ids[1],48000)) { return false; }
    if (memcmp(app.model.playing.id,ids[1],16) || !app.model.playing.title[0] || !app.model.playing_artwork[0]) { return false; }
    if (!action(APP_PREVIOUS,0) || !wait_state(h,PLAYER_PLAYING,ids[0],0)) { return false; }
    if (!action(APP_SHUFFLE,0) || !action(APP_REPEAT,0) || !action(APP_QUEUE,0)) { return false; }
    until=GetTickCount64()+5000;
    do { (void)app_update(&app,GetTickCount64()); (void)WaitForSingleObject(changed,100); }
    while ((!app.model.player.shuffled || app.model.player.repeat!=PLAYER_REPEAT_ONE) && GetTickCount64()<until);
    if (!app.model.player.shuffled || app.model.player.repeat!=PLAYER_REPEAT_ONE || app.now.queue.count!=2) { return false; }
    (void)app_enqueue(&app,(AppAction){.type=APP_GO_SETTINGS}); (void)app_update(&app,GetTickCount64());
    if (memcmp(app.model.playing.id,ids[0],16)) { return false; }
    c=(PlayerCommand){.type=PLAYER_PAUSE};
    return headless_submit(h,&c)==PLAYER_OK && wait_state(h,PLAYER_PAUSED,ids[0],0);
}
int main(void)
{
    wchar_t credentials[240], cache[240]; char tracks[2][37], country[3], catalogue[33], consent[4]; uint8_t ids[2][16];
    DWORD opt=GetEnvironmentVariableA("SPOTIC_PLAYER_BAR_LIVE",consent,sizeof(consent));
    DWORD a=GetEnvironmentVariableW(L"SPOTIC_HEADLESS_CREDENTIALS",credentials,240), b=GetEnvironmentVariableW(L"SPOTIC_HEADLESS_CACHE",cache,240);
    DWORD c=GetEnvironmentVariableA("SPOTIC_MEDIA_TRACK",tracks[0],37), d=GetEnvironmentVariableA("SPOTIC_MEDIA_TRACK_NEXT",tracks[1],37);
    DWORD e=GetEnvironmentVariableA("SPOTIC_MEDIA_COUNTRY",country,3), f=GetEnvironmentVariableA("SPOTIC_MEDIA_CATALOGUE",catalogue,33);
    if (opt!=1 || consent[0]!='1' || !a || a>=240 || !b || b>=240 || !c || c>=37 || !d || d>=37 || !e || e>=3 || !f || f>=33) {
        puts("SKIP: explicit SPOTIC_PLAYER_BAR_LIVE=1 and private credential/cache, two track URIs, country/catalogue required. This check starts account playback on WASAPI."); return 77;
    }
    if (media_gid_from_uri(tracks[0],ids[0])!=MEDIA_OK || media_gid_from_uri(tracks[1],ids[1])!=MEDIA_OK) { return 1; }
    changed=CreateEventW(NULL,FALSE,FALSE,NULL); if (!changed) { return 1; }
    HeadlessConfig config={.credential_path=credentials,.cache_directory=cache,.country=country,.catalogue=catalogue,
        .wasapi=true,.realtime=true,.state_changed=wake,.state_context=changed};
    Headless *h=NULL; bool ok=headless_open(&config,&h);
    if (ok) {
        AppServices services={.context=h,.submit=submit,.snapshot=snapshot,.track_info=info,.queue_snapshot=queue};
        ok=app_init(&app,&services,NULL,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK;
        if (ok) { (void)app_session(&app,true,true,GetTickCount64()); ok=controls(h,ids); app_dispose(&app); }
        HeadlessMetrics metrics; ok=headless_close(h,&metrics)&&metrics.balanced&&ok;
    }
    ok=CloseHandle(changed)&&ok;
    puts(ok ? "Release account/WASAPI: bar commands, frozen pause, seek, volume/mute, next/previous metadata, modes, queue and owner-originated pause passed. Artwork download/GPU and native SMTC remain separate checks." : "Release player-bar acceptance failed.");
    return ok ? 0 : 1;
}
