#include "media_controls_test.h"
#include "ui/app_desktop.h"
#include <windowsx.h>

static AppDesktop desktop;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES], core_memory[MEMORY_BUDGET_CORE];
static MediaPublication delivered;
static unsigned commands, publications, closed;
static PlayerCommand command;
static PlayerResult admission;
static MediaResult open_adapter(void *context, MediaControls *controls) { (void)context; CHECK(controls==&desktop.media); return MEDIA_OK; }
static MediaResult publish_adapter(void *context, const MediaPublication *p, uint32_t flags)
{
    (void)context; CHECK(flags); delivered=*p; ++publications; return MEDIA_OK;
}
static MediaResult close_adapter(void *context) { (void)context; ++closed; return MEDIA_OK; }
static PlayerResult submit(void *context, const PlayerCommand *c) { (void)context; command=*c; ++commands; return admission; }
void mt_app(void)
{
    Arena core={0}; EventQueue *events=NULL;
    CHECK(arena_init(&core,core_memory,sizeof(core_memory)) && event_queue_create(&core,16,&events)==CORE_OK);
    AppServices services={.submit=submit};
    CHECK(app_init(&desktop.app,&services,NULL,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    UiWindow window={.config={.workers=events}};
    CHECK(app_desktop_media_bind(&desktop,&window,(MediaAdapter){open_adapter,publish_adapter,close_adapter,NULL})==MEDIA_OK);
    AppModel *m=&desktop.app.model; m->logged_in=m->online=true;
    m->playing=(AppEntity){.id={1},.title="Bound title"}; strcpy_s(m->playing_artist,sizeof(m->playing_artist),"Bound artist");
    m->playing_artist_id[0]=m->playing_artwork_id[0]=1;
    strcpy_s(m->playing_artwork,sizeof(m->playing_artwork),"https://example.invalid/prepared-cover.png");
    CHECK(app_publish_player(&desktop.app,(PlayerSnapshot){.track_id={1},.generation=3,.rate=48000,.volume=1,
        .state=PLAYER_PLAYING,.position_frames=48000,.duration_frames=480000,.timeline_valid=true},0)==APP_OK);
    uint64_t now=GetTickCount64(); app_desktop_media_prepare(&desktop,now);
    CHECK(publications==1 && !strcmp(delivered.metadata.title,"Bound title") && !strcmp(delivered.metadata.artist,"Bound artist"));
    CHECK(!strcmp(delivered.metadata.artwork,m->playing_artwork) && delivered.position==10000000);
    LRESULT result=0;
    CHECK(app_desktop_input(&desktop,&window,WM_APPCOMMAND,0,(LPARAM)APPCOMMAND_MEDIA_NEXTTRACK<<16,&result) && result==TRUE);
    CHECK(app_desktop_input(&desktop,&window,WM_KEYDOWN,VK_MEDIA_NEXT_TRACK,1,&result));
    app_desktop_media_prepare(&desktop,now); CHECK(commands==1 && command.type==PLAYER_NEXT);
    CHECK(app_desktop_input(&desktop,&window,WM_APPCOMMAND,0,(LPARAM)APPCOMMAND_MEDIA_PLAY_PAUSE<<16,&result));
    app_desktop_media_prepare(&desktop,now); CHECK(commands==2 && command.type==PLAYER_PAUSE);
    admission=PLAYER_FULL;
    CHECK(app_desktop_input(&desktop,&window,WM_APPCOMMAND,0,(LPARAM)APPCOMMAND_MEDIA_PREVIOUSTRACK<<16,&result));
    app_desktop_media_prepare(&desktop,now); CHECK(desktop.media_result==MEDIA_FULL && m->last_result==APP_FULL);
    admission=PLAYER_OK;
    CHECK(app_session(&desktop.app,false,true,now)==APP_UNAVAILABLE); app_desktop_media_prepare(&desktop,now);
    CHECK(!delivered.metadata.title[0] && !delivered.metadata.artist[0] && !delivered.metadata.artwork[0] && !delivered.capabilities);
    CHECK(app_desktop_media_close(&desktop) && closed==1); app_dispose(&desktop.app);
    CHECK(event_queue_close(events)==CORE_OK); Event batch[16]; size_t n;
    (void)event_drain(events,batch,16,&n); CHECK(event_queue_destroy(events)==CORE_OK); arena_dispose(&core);
}
