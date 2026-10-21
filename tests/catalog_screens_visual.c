#include "catalog_screens_test.h"
#include <stdio.h>

static FontCache fonts;
static UiRenderer renderer;
static UiRenderFrame frame;
static int capture(const char *name, const char *size, uint32_t width, uint32_t height, uint16_t dpi)
{
    UiRendererConfig config={.width=width,.height=height,.warp=true};
    UiRenderResult r=ui_renderer_create(&renderer,&config); if (r==UI_RENDER_UNSUPPORTED) { return 77; } CHECK(r==UI_RENDER_OK);
    FontMeasureContext text={.cache=&fonts,.config={.dpi=dpi,.weight=400,.max_width=260,.ellipsis=true}};
    app_bind_fonts(&cs_app,&text);
    for (unsigned n=0;n<100;++n) {
        FontResult prepared=font_prepare_pending(&fonts), uploaded=font_upload_renderer(&fonts,&renderer);
        if (prepared!=FONT_OK || uploaded!=FONT_OK) {
            fprintf(stderr,"capture %s %s %u: font prepare=%u upload=%u\n",name,size,dpi,(unsigned)prepared,(unsigned)uploaded);
        }
        CHECK(prepared==FONT_OK && uploaded==FONT_OK);
        Clay_RenderCommandArray c=cs_layout((float)width,(float)height,(float)dpi);
        CHECK(ui_frame_begin(&frame,width,height,(float)dpi/96)==UI_RENDER_OK);
        UiClayBridge bridge={.custom=app_render_custom,.context=&cs_app};
        CHECK(ui_frame_clay(&frame,c,&bridge)==UI_RENDER_OK);
        const float clear[4]={0,0,0,1}; CHECK(ui_renderer_submit(&renderer,&frame,clear)==UI_RENDER_OK);
    }
    CHECK(font_stats(&fonts).pending==0);
    size_t bytes=(size_t)width*height*4;
    void *rgba=VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); CHECK(rgba);
    CHECK(ui_renderer_readback(&renderer,rgba,(size_t)width*4,bytes)==UI_RENDER_OK);
    char path[160]; CHECK(snprintf(path,sizeof(path),"catalog-%s-%s-%u.rgba",name,size,dpi)>0);
    FILE *file=NULL; CHECK(fopen_s(&file,path,"wb")==0 && file);
    CHECK(fwrite(rgba,1,bytes,file)==bytes && fclose(file)==0 && VirtualFree(rgba,0,MEM_RELEASE));
    cs_app.fonts=NULL; ui_renderer_destroy(&renderer);
    CHECK(ui_renderer_stats(&renderer).resources==0 && ui_renderer_stats(&renderer).textures==0);
    CHECK(font_stats(&fonts).com_objects==1 && font_cache_reset(&fonts)==FONT_OK);
    return 0;
}
int cs_visual(void)
{
    CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    /* Warm OS font resources before measuring app/worker handle lifetimes. */
    FontConfig warm={.dpi=96,.size=16,.weight=400,.max_width=800};
    CHECK(font_request_text(&fonts,(Clay_StringSlice){.chars="Warm-up",.length=7},&warm)==FONT_PENDING_WORK);
    CHECK(font_prepare_pending(&fonts)==FONT_OK);
    cs_open(false); cs_query("music"); cs_tick(250); cs_fixture("search");
    const uint32_t widths[]={400,1100,1440}; const uint16_t dpi[]={96,120,144,192};
    const char *sizes[]={"narrow","default","wide"};
    const char *names[]={"search","artist","album","lyrics"};
    int result=0;
    for (unsigned screen=0;screen<4 && !result;++screen) {
        if (screen==1) { cs_action(APP_CATALOG_SELECT,0); cs_fixture("artist"); cs_action(APP_CATALOG_SECTION,ARTIST_ALBUMS); cs_fixture("artist"); }
        if (screen==2) { cs_action(APP_CATALOG_SELECT,0); cs_fixture("album"); }
        if (screen==3) { cs_action(APP_CATALOG_SELECT,0); cs_action(APP_GO_LYRICS,0); cs_fixture("lyrics"); cs_player.position_frames=144000; cs_tick(300); }
        for (unsigned size=0;size<3 && !result;++size) {
            for (unsigned scale=0;scale<4 && !result;++scale) {
                result=capture(names[screen],sizes[size],widths[size]*dpi[scale]/96,720u*dpi[scale]/96,dpi[scale]);
            }
        }
    }
    if (!result) {
        cs_action(APP_GO_SEARCH,0); cs_query("missing"); cs_tick(cs_now+250);
        for (unsigned scale=0;scale<4 && !result;++scale) {
            result=capture("search-loading","default",1100u*dpi[scale]/96,720u*dpi[scale]/96,dpi[scale]);
        }
        cs_response("{\"data\":{\"searchV2\":{\"topResultsV2\":{\"itemsV2\":[]}}}}",200);
        for (unsigned scale=0;scale<4 && !result;++scale) {
            result=capture("search-empty","default",1100u*dpi[scale]/96,720u*dpi[scale]/96,dpi[scale]);
        }
        cs_action(APP_RETRY,0); cs_tick(cs_now+250); cs_response("",500);
        for (unsigned scale=0;scale<4 && !result;++scale) {
            result=capture("search-error","default",1100u*dpi[scale]/96,720u*dpi[scale]/96,dpi[scale]);
        }
        cs_action(APP_RETRY,0); cs_tick(cs_now+250); cs_fixture("search");
        (void)cs_layout(1100,720,96); cs_app.widgets.focus=CLAY_ID("CatalogList").id;
        cs_app.widgets.lists[0].focus=app_catalog_key(&cs_app,2);
        CHECK(app_context_open(&cs_app,0,0,true)==APP_OK);
        for (unsigned scale=0;scale<4 && !result;++scale) {
            result=capture("search-menu","default",1100u*dpi[scale]/96,720u*dpi[scale]/96,dpi[scale]);
        }
        app_context_close(&cs_app);
    }
    font_cache_destroy(&fonts); CHECK(font_stats(&fonts).com_objects==0);
    cs_close_visual(); return result;
}
