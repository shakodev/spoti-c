#include "ui/components.h"
#include "ui/font.h"
#include "support/test_check.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static UiWidgets ui;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static FontCache fonts;
static UiRenderer renderer;
static UiRenderFrame frame;
static void failed(void *context, UiWidgetResult error) { (void)context; (void)error; CHECK(false); }
static UiRenderResult custom(void *context, UiRenderFrame *f, const Clay_RenderCommand *c)
{
    if (ui_icon_command(c)) { return ui_icon_render(f,c); }
    if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
        FontMeasureContext *text=context; UiRenderResult r=font_render_clay(text,f,c);
        return r==UI_RENDER_FULL && font_stats(text->cache).pending ? UI_RENDER_OK : r;
    }
    return UI_RENDER_UNSUPPORTED;
}
static Clay_RenderCommandArray showcase(void)
{
    CHECK(ui_widgets_begin(&ui,800,640,(UiWidgetInput){.x=220,.y=90})==UI_WIDGET_OK);
    ui.focus=CLAY_IDI("Showcase",2).id;
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_GROW()},.padding=CLAY_PADDING_ALL(24),
        .childGap=16,.layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor=UI_PANEL}) {
        CLAY_TEXT(CLAY_STRING("Native visual foundations"),CLAY_TEXT_CONFIG({.fontId=FONT_FACE_BOLD,.fontSize=24,.textColor=UI_TEXT}));
        CLAY_AUTO_ID({.layout={.childGap=16}}) {
            for (uint32_t n=0;n<4;++n) {
                (void)ui_component_button(&ui,CLAY_IDI("Showcase",n),CLAY_STRING("Play"),144,48,n==3,
                    (UiButtonStyle){.kind=n==0 ? UI_BUTTON_PRIMARY : UI_BUTTON_SECONDARY,.icon=UI_ICON_PLAY});
            }
        }
        CLAY_AUTO_ID({.layout={.childGap=8}}) {
            (void)ui_component_button(&ui,CLAY_ID("All"),CLAY_STRING("All"),64,32,false,(UiButtonStyle){.kind=UI_BUTTON_TAB,.selected=true});
            (void)ui_component_button(&ui,CLAY_ID("Songs"),CLAY_STRING("Songs"),88,32,false,(UiButtonStyle){.kind=UI_BUTTON_TAB});
            (void)ui_component_button(&ui,CLAY_ID("Albums"),CLAY_STRING("Albums"),88,32,false,(UiButtonStyle){.kind=UI_BUTTON_TAB});
        }
        (void)ui_component_button(&ui,CLAY_ID("Track"),CLAY_STRING("Current track — Unicode / العربية"),680,48,false,
            (UiButtonStyle){.kind=UI_BUTTON_ROW,.current=true,.icon=UI_ICON_PAUSE});
        CLAY_AUTO_ID({.layout={.childGap=16}}) {
            for (UiIcon icon=UI_ICON_PLAY;icon<UI_ICON_COUNT;++icon) { ui_icon(&ui,icon,32,UI_TEXT); }
        }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(280),CLAY_SIZING_FIT()},.padding=CLAY_PADDING_ALL(4),
            .layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor=UI_HIGHLIGHT,.cornerRadius=CLAY_CORNER_RADIUS(4)}) {
            (void)ui_component_button(&ui,CLAY_ID("MenuPlay"),CLAY_STRING("Play first track"),272,40,false,(UiButtonStyle){.kind=UI_BUTTON_MENU,.icon=UI_ICON_PLAY});
            (void)ui_component_button(&ui,CLAY_ID("MenuUnavailable"),CLAY_STRING("Save unavailable"),272,40,true,(UiButtonStyle){.kind=UI_BUTTON_MENU,.icon=UI_ICON_PLUS});
        }
        CLAY_TEXT(CLAY_STRING("Unavailable actions are disabled. Segoe UI is the system font fallback."),
            CLAY_TEXT_CONFIG({.fontSize=14,.textColor=UI_SUBDUED}));
    }
    return ui_widgets_end(&ui);
}
int ui_components_visual(void)
{
    CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),failed,NULL)==UI_WIDGET_OK);
    CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    const uint16_t dpis[]={96,120,144,192};
    for (unsigned pass=0;pass<4;++pass) {
        uint32_t width=800u*dpis[pass]/96, height=640u*dpis[pass]/96;
        UiRendererConfig config={.width=width,.height=height,.warp=true};
        UiRenderResult r=ui_renderer_create(&renderer,&config); if (r==UI_RENDER_UNSUPPORTED) { return 77; } CHECK(r==UI_RENDER_OK);
        FontMeasureContext text={.cache=&fonts,.config={.dpi=dpis[pass],.weight=400,.max_width=700,.ellipsis=true}};
        Clay_SetCurrentContext(ui.clay); font_bind_clay(&text);
        for (unsigned n=0;n<100;++n) {
            CHECK(font_prepare_pending(&fonts)==FONT_OK && font_upload_renderer(&fonts,&renderer)==FONT_OK);
            Clay_RenderCommandArray commands=showcase(); CHECK(ui.error==UI_WIDGET_OK);
            CHECK(ui_frame_begin(&frame,width,height,(float)dpis[pass]/96)==UI_RENDER_OK);
            CHECK(ui_frame_clay(&frame,commands,&(UiClayBridge){.custom=custom,.context=&text})==UI_RENDER_OK);
            CHECK(ui_renderer_submit(&renderer,&frame,(float[4]){0,0,0,1})==UI_RENDER_OK);
        }
        CHECK(font_stats(&fonts).pending==0);
        size_t bytes=(size_t)width*height*4;
        void *rgba=VirtualAlloc(NULL,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); CHECK(rgba);
        CHECK(ui_renderer_readback(&renderer,rgba,(size_t)width*4,bytes)==UI_RENDER_OK);
        bool sampled=false;
        for (uint32_t n=0;n<frame.quad_count;++n) {
            const QuadInstance *q=&frame.quads[n];
            if (q->effect[0]!=UI_QUAD_ICON || q->effect[1]!=UI_ICON_PLAY || q->color[0]!=1) { continue; }
            float scale=(float)dpis[pass]/96;
            size_t center=((size_t)((q->box[1]+q->box[3]/2)*scale)*width+(uint32_t)((q->box[0]+q->box[2]/2)*scale))*4;
            size_t corner=((size_t)((q->box[1]+1)*scale)*width+(uint32_t)((q->box[0]+1)*scale))*4;
            CHECK(((unsigned char *)rgba)[center]>240 && ((unsigned char *)rgba)[corner]<80);
            sampled=true; break;
        }
        CHECK(sampled); /* A solid Clay background would fail the corner sample. */
        char path[128]; (void)snprintf(path,sizeof(path),"components-default-%u.rgba",dpis[pass]);
        FILE *file=NULL; CHECK(fopen_s(&file,path,"wb")==0 && file);
        CHECK(fwrite(rgba,1,bytes,file)==bytes && fclose(file)==0 && VirtualFree(rgba,0,MEM_RELEASE));
        CHECK(font_stats(&fonts).com_objects==1);
        ui_renderer_destroy(&renderer); CHECK(font_cache_reset(&fonts)==FONT_OK);
    }
    font_cache_destroy(&fonts); CHECK(ui_widgets_dispose(&ui)==UI_WIDGET_OK); return 0;
}
