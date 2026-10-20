/* Explicit WARP visual check; synthetic metadata/artwork, no account/device. */
#include "app_shell_support.h"
#include "artwork_fixtures.h"
#include <stdio.h>

static UiRenderer renderer;
static UiRenderFrame frame;
static FontCache fonts;
static _Alignas(16) unsigned char art_storage[65536], workspace[ART_DECODE_BYTES];
static unsigned char pixels[ART_UPLOAD_BYTES], core_memory[MEMORY_BUDGET_CORE];
static Artwork *art;
static PlayerSnapshot snapshot;
static PlayerSnapshot current(void *context) { (void)context; return snapshot; }
static PlayerResult submit(void *context,const PlayerCommand *command) { (void)context; (void)command; return PLAYER_OK; }
static ArtResult fetch(void *context,const wchar_t *url,void *buffer,size_t capacity,size_t *bytes,const CancelToken *cancel)
{
    (void)context; (void)url; (void)cancel; CHECK(capacity>=sizeof(art_fixture_png_medium));
    memcpy(buffer,art_fixture_png_medium,sizeof(art_fixture_png_medium)); *bytes=sizeof(art_fixture_png_medium); return ART_OK;
}
static void capture(const char *name,uint32_t width,uint32_t height,uint16_t dpi)
{
    CHECK(ui_renderer_resize(&renderer,width,height)==UI_RENDER_OK);
    app.fonts->config.dpi=dpi;
    for (unsigned i=0;i<80;++i) {
        (void)app_update(&app,1000);
        CHECK(font_prepare_pending(&fonts)==FONT_OK && font_upload_renderer(&fonts,&renderer)==FONT_OK);
        Clay_RenderCommandArray commands=shell_layout((float)width,(float)height,(float)dpi);
        CHECK(ui_frame_begin(&frame,width,height,(float)dpi/96)==UI_RENDER_OK);
        UiClayBridge bridge={.custom=app_render_custom,.context=&app};
        CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK);
        const float clear[4]={0,0,0,1}; CHECK(ui_renderer_submit(&renderer,&frame,clear)==UI_RENDER_OK);
    }
    CHECK(font_stats(&fonts).pending==0);
    size_t bytes=(size_t)width*height*4; void *rgba=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); CHECK(rgba);
    CHECK(ui_renderer_readback(&renderer,rgba,(size_t)width*4,bytes)==UI_RENDER_OK);
    if (app.model.logged_in) {
        Clay_BoundingBox box=Clay_GetElementData(CLAY_ID("PlayerArtwork")).boundingBox;
        uint32_t x=(uint32_t)((box.x+box.width/2)*dpi/96), y=(uint32_t)((box.y+box.height/2)*dpi/96);
        const unsigned char *pixel=(const unsigned char *)rgba+((size_t)y*width+x)*4;
        CHECK(pixel[0]==32 && pixel[1]==96 && pixel[2]==160);
    }
    char path[160]; CHECK(sprintf_s(path,sizeof(path),"player-bar-%s-%ux%u.rgba",name,width,height)>0);
    FILE *file=NULL; CHECK(fopen_s(&file,path,"wb")==0 && file);
    CHECK(fwrite(rgba,1,bytes,file)==bytes && fclose(file)==0 && VirtualFree(rgba,0,MEM_RELEASE));
    printf("%s dpi=%u artwork_uploads=%llu\n",path,dpi,(unsigned long long)artwork_stats(art).uploaded);
}
static DWORD WINAPI run(void *context)
{
    (void)context; Arena core; Worker *worker;
    CHECK(arena_init(&core,core_memory,sizeof(core_memory)) && worker_start(&core,4,(CoreClock){0},&worker)==CORE_OK);
    UiRendererConfig rc={.width=1600,.height=900,.warp=true}; CHECK(ui_renderer_create(&renderer,&rc)==UI_RENDER_OK);
    ArtworkConfig ac={.worker=worker,.fetch=fetch,.gpu=artwork_renderer_gpu(&renderer),.decode_workspace=workspace,.upload=pixels};
    CHECK(artwork_init(art_storage,sizeof(art_storage),&ac,&art)==ART_OK);
    CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    FontMeasureContext text={.cache=&fonts,.config={.dpi=96,.weight=400,.max_width=260,.ellipsis=true}};
    shell_open(NULL); app.services.submit=submit; app.services.snapshot=current; app.services.artwork=art;
    CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home"); app_bind_fonts(&app,&text);
    AppEntity track={.kind=COLLECTION_TRACK,.title="Synthetic track with real artwork binding",.playable=true}; track.id[15]=1;
    CHECK(app_track_remember(&app,&track,"Synthetic artist","https://fixture.invalid/cover.png")==APP_OK);
    snapshot=(PlayerSnapshot){.generation=1,.state=PLAYER_PAUSED,.volume=0.68f,.rate=48000,.position_frames=480000,
        .duration_frames=9600000,.timeline_valid=true}; snapshot.track_id[15]=1;
    capture("normal-paused",1600,900,96);
    CHECK(artwork_stats(art).uploaded==1);
    snapshot.state=PLAYER_PLAYING; snapshot.repeat=PLAYER_REPEAT_ALL; snapshot.shuffled=true;
    capture("normal-playing",1600,900,96); capture("narrow-playing",400,720,96); capture("scaled-playing",1650,1080,144);
    strcpy_s(track.title,sizeof(track.title),"\xd7\x90\xd7\x91\xd7\x92 \xd7\x93\xd7\x94\xd7\x95 | A long title that must stay inside the metadata group without moving transport controls");
    CHECK(app_track_remember(&app,&track,"\xd7\x90\xd7\x91\xd7\x92 | Synthetic artist","")==APP_OK);
    capture("rtl-long",1100,720,96);
    snapshot.state=PLAYER_BUFFERING; snapshot.timeline_valid=false; capture("buffering",1100,720,96);
    snapshot.state=PLAYER_ERROR; capture("error",1100,720,96);
    snapshot.state=PLAYER_STOPPED; capture("stopped",1100,720,96);
    CHECK(app_session(&app,false,true,0)==APP_UNAVAILABLE); capture("signed-out",400,720,96);
    app.fonts=NULL; font_cache_destroy(&fonts); shell_close();
    while (artwork_stop(art)==ART_PENDING) { CHECK(WaitForSingleObject(artwork_wait_handle(art),5000)==WAIT_OBJECT_0); }
    CHECK(worker_stop(worker)==CORE_OK && worker_join(worker,5000)==CORE_OK); arena_dispose(&core);
    ui_renderer_destroy(&renderer); return 0;
}
int main(void)
{
    HANDLE thread=CreateThread(NULL,0,run,NULL,0,NULL); CHECK(thread);
    CHECK(WaitForSingleObject(thread,90000)==WAIT_OBJECT_0);
    DWORD result; CHECK(GetExitCodeThread(thread,&result) && CloseHandle(thread)); return (int)result;
}
