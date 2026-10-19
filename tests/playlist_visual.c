#include "playlist_fixture.h"
#include "ui/app.h"
#include <stdio.h>
#include <string.h>

static FontCache fonts;
static UiRenderer renderer;
static UiRenderFrame frame;
int playlist_visual(App *app)
{
    const uint32_t widths[]={500,1375,1800}, heights[]={800,900,1125};
    const char *names[]={"narrow","default","wide"};
    FontMeasureContext text={.cache=&fonts,.config={.dpi=120,.weight=400,.max_width=260,.ellipsis=true}};
    for (unsigned i=0;i<3;++i) {
        UiRendererConfig config={.width=widths[i],.height=heights[i],.warp=true};
        UiRenderResult result=ui_renderer_create(&renderer,&config); if (result==UI_RENDER_UNSUPPORTED) { return 77; }
        CHECK(result==UI_RENDER_OK); CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK); app_bind_fonts(app,&text);
        for (unsigned n=0;n<80;++n) {
            CHECK(font_prepare_pending(&fonts)==FONT_OK && font_upload_renderer(&fonts,&renderer)==FONT_OK);
            Clay_RenderCommandArray commands=app_layout(app,(float)widths[i],(float)heights[i],120,(UiWidgetInput){.x=-1,.y=-1});
            CHECK(app->widgets.error==UI_WIDGET_OK);
            CHECK(ui_frame_begin(&frame,widths[i],heights[i],1.25f)==UI_RENDER_OK);
            UiClayBridge bridge={.custom=app_render_custom,.context=app};
            CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK);
            const float clear[4]={0,0,0,1}; CHECK(ui_renderer_submit(&renderer,&frame,clear)==UI_RENDER_OK);
        }
        CHECK(font_stats(&fonts).pending==0);
        size_t bytes=(size_t)widths[i]*heights[i]*4;
        void *rgba=VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); CHECK(rgba);
        CHECK(ui_renderer_readback(&renderer,rgba,(size_t)widths[i]*4,bytes)==UI_RENDER_OK);
        char path[128]; CHECK(snprintf(path,sizeof(path),"playlist-%s.rgba",names[i])>0);
        FILE *file=NULL; CHECK(fopen_s(&file,path,"wb")==0 && file);
        CHECK(fwrite(rgba,1,bytes,file)==bytes && fclose(file)==0 && VirtualFree(rgba,0,MEM_RELEASE));
        printf("playlist visual: %s %ux%u\n",names[i],widths[i],heights[i]);
        font_cache_destroy(&fonts); app->fonts=NULL; ui_renderer_destroy(&renderer); memset(&fonts,0,sizeof(fonts));
    }
    return 0;
}
