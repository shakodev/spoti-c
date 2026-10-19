#include "media_controls_test.h"
#include "support/ui_heap_watch.h"
#include <stdio.h>

MediaControls mc;
MediaFake mf;
MediaMetadata mm;
PlayerSnapshot mp;
static MediaResult open_fake(void *context, MediaControls *controls)
{
    MediaFake *f=context; CHECK(controls==&mc); ++f->opened; return f->open_result;
}
static MediaResult publish_fake(void *context, const MediaPublication *p, uint32_t flags)
{
    MediaFake *f=context; ++f->updates; f->publication=*p; f->flags=flags; return f->publish_result;
}
static MediaResult close_fake(void *context) { MediaFake *f=context; ++f->closed; return f->close_result; }
static void wake(void *context) { atomic_fetch_add(&((MediaFake *)context)->wakes,1); }
static PlayerResult submit(void *context, const PlayerCommand *c)
{
    MediaFake *f=context;
    if (f->close_on_submit) { CHECK(media_controls_close(&mc)==MEDIA_OK); }
    if (f->submit_result!=PLAYER_OK) { return f->submit_result; }
    CHECK(f->submitted<128); f->commands[f->submitted++]=*c; return PLAYER_OK;
}
void mt_open(void)
{
    memset(&mf,0,sizeof(mf)); atomic_init(&mf.wakes,0);
    mm=(MediaMetadata){.track_id={1},.title="Synthetic track — 東京",.artist="Test artist",.artwork="https://example.invalid/art.png"};
    mp=(PlayerSnapshot){.track_id={1},.generation=10,.rate=48000,.duration_frames=480000,
        .position_frames=48000,.state=PLAYER_PLAYING,.volume=1,.timeline_valid=true};
    CHECK(media_controls_init(&mc,(MediaConfig){submit,wake,&mf},(MediaAdapter){open_fake,publish_fake,close_fake,&mf})==MEDIA_OK);
}
void mt_publish(uint64_t now) { CHECK(media_controls_publish(&mc,&mm,mp,63,now)==MEDIA_OK); }
void mt_close(void) { CHECK(media_controls_close(&mc)==MEDIA_OK && !mc.alive); }
static void publication(void)
{
    mt_open(); mt_publish(0);
    CHECK(mf.updates==1 && mf.flags==7 && mf.publication.position==10000000 && mf.publication.duration==100000000);
    CHECK(mf.publication.status==MEDIA_STATUS_PLAYING && mf.publication.capabilities==63);
    ui_heap_begin(); for (unsigned i=0;i<2000;++i) { mt_publish(i); } ui_heap_end(); CHECK(mf.updates==1);
    mp.position_frames=96000; mt_publish(999); CHECK(mf.updates==1);
    mt_publish(1000); CHECK(mf.updates==2 && mf.flags==MEDIA_TIMELINE);
    mp.state=PLAYER_PAUSED; mt_publish(1001); CHECK(mf.updates==3 && mf.publication.status==MEDIA_STATUS_PAUSED);
    mp.position_frames=24000; ++mp.generation; mt_publish(1002);
    CHECK(mf.publication.position==5000000 && mf.flags==MEDIA_TIMELINE);
    mp.timeline_valid=false; mp.state=PLAYER_BUFFERING; mt_publish(1003);
    CHECK(!(mf.publication.capabilities&MEDIA_CAN_SEEK) && mf.publication.status==MEDIA_STATUS_CHANGING);
    mp.state=PLAYER_PLAYING; mp.timeline_valid=true; mp.duration_frames=0; mt_publish(1004);
    CHECK(!(mf.publication.capabilities&MEDIA_CAN_SEEK));
    mp.duration_frames=48000; mp.position_frames=96000; mt_publish(2004); CHECK(mf.publication.position==10000000);
    mp.duration_frames=UINT64_MAX; mp.rate=1;
    CHECK(media_controls_publish(&mc,&mm,mp,63,2005)==MEDIA_LIMIT);
    mp.duration_frames=480000; mp.rate=48000; mp.position_frames=0;
    mf.publish_result=MEDIA_PLATFORM_ERROR; strcpy_s(mm.title,sizeof(mm.title),"replacement");
    unsigned before=mf.updates; CHECK(media_controls_publish(&mc,&mm,mp,63,3000)==MEDIA_PLATFORM_ERROR);
    mf.publish_result=MEDIA_OK; mt_publish(3000); CHECK(mf.updates==before+2);
    mm=(MediaMetadata){0}; mp=(PlayerSnapshot){0}; mt_publish(3001);
    CHECK(mf.publication.status==MEDIA_STATUS_CLOSED && !mf.publication.capabilities && !mf.publication.metadata.artwork[0]);
    strcpy_s(mm.artwork,sizeof(mm.artwork),"file:///private");
    CHECK(media_controls_publish(&mc,&mm,mp,63,4000)==MEDIA_INVALID);
    mm.artwork[0]=0; mp.state=(PlayerState)-1;
    CHECK(media_controls_publish(&mc,&mm,mp,63,4000)==MEDIA_INVALID);
    mp.state=PLAYER_STOPPED; memcpy(mm.title,"\xff",2);
    CHECK(media_controls_publish(&mc,&mm,mp,63,4000)==MEDIA_INVALID);
    memset(mm.title,'x',sizeof(mm.title));
    CHECK(media_controls_publish(&mc,&mm,mp,63,4000)==MEDIA_INVALID);
    mt_close(); CHECK(mf.closed==1 && media_controls_close(&mc)==MEDIA_OK && mf.closed==1);
}
static void initialization(void)
{
    const MediaResult failures[]={MEDIA_UNSUPPORTED,MEDIA_PLATFORM_ERROR};
    for (unsigned i=0;i<2;++i) {
        mt_open(); mt_close(); mf.open_result=failures[i]; unsigned closed=mf.closed;
        CHECK(media_controls_init(&mc,(MediaConfig){submit,wake,&mf},
            (MediaAdapter){open_fake,publish_fake,close_fake,&mf})==failures[i]);
        CHECK(mc.alive==(failures[i]==MEDIA_UNSUPPORTED));
        CHECK(mf.closed==closed+(failures[i]==MEDIA_PLATFORM_ERROR));
        if (mc.alive) { mt_publish(0); mt_close(); }
        else { CHECK(media_controls_post(&mc,MEDIA_PLAY,MEDIA_SYSTEM,0,0)==MEDIA_CLOSED); }
    }
}
static void commands(void)
{
    mt_open(); mp.state=PLAYER_PAUSED; mt_publish(0);
    CHECK(media_controls_post(&mc,MEDIA_PLAY,MEDIA_SYSTEM,0,100)==MEDIA_OK);
    CHECK(media_controls_appcommand(&mc,APPCOMMAND_MEDIA_PLAY_PAUSE,101)==MEDIA_OK);
    CHECK(media_controls_key(&mc,VK_MEDIA_PLAY_PAUSE,false,102)==MEDIA_OK);
    CHECK(media_controls_pump(&mc)==MEDIA_OK && mf.submitted==1 && mf.commands[0].type==PLAYER_PLAY && mc.stats.duplicates==2);
    mp.state=PLAYER_PLAYING; mt_publish(103);
    CHECK(media_controls_appcommand(&mc,APPCOMMAND_MEDIA_PAUSE,104)==MEDIA_OK);
    CHECK(media_controls_pump(&mc)==MEDIA_OK && mf.submitted==2 && mf.commands[1].type==PLAYER_PAUSE);
    CHECK(media_controls_key(&mc,VK_MEDIA_PLAY_PAUSE,true,105)==MEDIA_OK && !mc.count);
    CHECK(media_controls_post(&mc,MEDIA_SEEK,MEDIA_SYSTEM,25000000,400)==MEDIA_OK);
    CHECK(media_controls_pump(&mc)==MEDIA_OK && mf.commands[2].position_frames==120000);
    mp.timeline_valid=false; mt_publish(401);
    CHECK(media_controls_post(&mc,MEDIA_SEEK,MEDIA_SYSTEM,0,700)==MEDIA_OK && media_controls_pump(&mc)==MEDIA_UNSUPPORTED);
    mp.timeline_valid=true; mt_publish(701);
    CHECK(media_controls_post(&mc,MEDIA_NEXT,MEDIA_SYSTEM,0,1000)==MEDIA_OK);
    CHECK(media_controls_pump(&mc)==MEDIA_OK); unsigned n=mf.submitted;
    mp.track_id[0]=mm.track_id[0]=2; ++mp.generation; mt_publish(1001);
    CHECK(media_controls_key(&mc,VK_MEDIA_NEXT_TRACK,false,1002)==MEDIA_OK && media_controls_pump(&mc)==MEDIA_OK && mf.submitted==n);
    CHECK(media_controls_key(&mc,VK_MEDIA_NEXT_TRACK,false,1300)==MEDIA_OK && media_controls_pump(&mc)==MEDIA_OK && mf.submitted==n+1);
    CHECK(media_controls_post(&mc,MEDIA_PREVIOUS,MEDIA_SYSTEM,0,1600)==MEDIA_OK);
    ++mp.generation; mt_publish(1601); CHECK(media_controls_pump(&mc)==MEDIA_STALE && mc.stats.stale==1);
    mf.submit_result=PLAYER_FULL;
    CHECK(media_controls_post(&mc,MEDIA_NEXT,MEDIA_SYSTEM,0,2000)==MEDIA_OK && media_controls_pump(&mc)==MEDIA_FULL);
    mf.submit_result=PLAYER_OK;
    CHECK(media_controls_key(&mc,VK_MEDIA_NEXT_TRACK,false,2001)==MEDIA_OK && media_controls_pump(&mc)==MEDIA_OK);
    CHECK(media_controls_appcommand(&mc,APPCOMMAND_VOLUME_UP,2002)==MEDIA_UNSUPPORTED);
    CHECK(media_controls_key(&mc,VK_RETURN,false,2002)==MEDIA_UNSUPPORTED);
    CHECK(media_controls_post(&mc,MEDIA_SEEK,MEDIA_SYSTEM,-1,0)==MEDIA_INVALID);
    mt_close();
}
static DWORD WINAPI producer(void *context)
{
    uintptr_t number=(uintptr_t)context;
    for (unsigned i=0;i<4;++i) { CHECK(media_controls_post(&mc,MEDIA_NEXT,MEDIA_SYSTEM,0,number*1000+i)==MEDIA_OK); } return 0;
}
static void bounded_queue(void)
{
    mt_open(); mt_publish(0); HANDLE threads[4];
    for (uintptr_t i=0;i<4;++i) { threads[i]=CreateThread(NULL,0,producer,(void *)i,0,NULL); CHECK(threads[i]); }
    CHECK(WaitForMultipleObjects(4,threads,true,5000)==WAIT_OBJECT_0);
    for (unsigned i=0;i<4;++i) { CHECK(CloseHandle(threads[i])); }
    CHECK(mc.count==16 && media_controls_post(&mc,MEDIA_STOP,MEDIA_SYSTEM,0,5000)==MEDIA_FULL);
    CHECK(media_controls_pump(&mc)==MEDIA_OK && mf.submitted==16 && mc.stats.full==1 && atomic_load(&mf.wakes)==17);
    mf.close_result=MEDIA_PLATFORM_ERROR; CHECK(media_controls_close(&mc)==MEDIA_PLATFORM_ERROR && mc.alive && mc.closing);
    CHECK(media_controls_post(&mc,MEDIA_PLAY,MEDIA_SYSTEM,0,0)==MEDIA_CLOSED);
    mf.close_result=MEDIA_OK; mt_close();
    mt_open(); mt_publish(0); mf.close_on_submit=true;
    CHECK(media_controls_post(&mc,MEDIA_NEXT,MEDIA_SYSTEM,0,0)==MEDIA_OK && media_controls_pump(&mc)==MEDIA_OK && !mc.alive);
}
int main(int argc, char **argv)
{
    publication(); initialization(); commands(); bounded_queue(); mt_events(); mt_app();
    if (argc==2 && !strcmp(argv[1],"--native")) { return mt_native(); }
    puts("app_media_controls: publication, audible timeline, media keys, bounded MPSC, duplicates and COM callback lifetimes passed"); return 0;
}
