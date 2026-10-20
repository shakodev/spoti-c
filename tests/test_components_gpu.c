#include "ui/component_showcase.h"
#include "ui/font.h"
#include "ui/font_roles.h"
#include "support/test_check.h"
#include "support/ui_heap_watch.h"
#include <stdio.h>
#include <string.h>

static _Alignas(64) unsigned char clay_memory[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static unsigned char pixels[UI_SHOWCASE_WIDTH * 2u * UI_SHOWCASE_HEIGHT * 2u * 4u];
static UiWidgets widgets;
static UiRenderer renderer;
static UiRenderFrame frame;
static FontCache fonts;
static FontMeasureContext text;
typedef struct Measure {
    FontConfig config;
    Clay_Dimensions dimensions;
    uint32_t length;
    char text[192];
} Measure;
static Measure measurements[256];
static uint32_t measure_count, measure_next;
static bool same_config(FontConfig a,FontConfig b)
{
    return a.text_generation==b.text_generation && a.face==b.face && a.size==b.size && a.dpi==b.dpi &&
        a.weight==b.weight && a.letter_spacing==b.letter_spacing && a.line_height==b.line_height &&
        a.max_width==b.max_width && a.italic==b.italic && a.rtl==b.rtl && a.ellipsis==b.ellipsis &&
        a.measure_only==b.measure_only;
}
static Clay_Dimensions measure(Clay_StringSlice slice,Clay_TextElementConfig *style,void *context)
{
    FontMeasureContext *font=context;
    CHECK(slice.length>=0 && slice.length<(int32_t)sizeof(measurements[0].text));
    FontConfig key=font_config_for_clay(font->config,style->fontId,style->fontSize,
        style->letterSpacing,style->lineHeight);
    key.measure_only=true;
    for (uint32_t i=0;i<measure_count;++i) {
        Measure *m=&measurements[i];
        if (m->length==(uint32_t)slice.length && same_config(m->config,key) &&
            (!m->length || !memcmp(m->text,slice.chars,m->length))) {
            font->last_result=FONT_OK; return m->dimensions;
        }
    }
    Clay_Dimensions dimensions=font_measure(slice,style,font);
    if (font->last_result==FONT_OK) {
        uint32_t slot=measure_count<256 ? measure_count++ : measure_next++%256;
        Measure *m=&measurements[slot];
        m->config=key; m->length=(uint32_t)slice.length; m->dimensions=dimensions;
        if (m->length) { memcpy(m->text,slice.chars,m->length); }
    }
    return dimensions;
}
static void fail(void *context, UiWidgetResult result)
{
    (void)context; fprintf(stderr,"showcase widget error %d\n",(int)result); CHECK(false);
}
static Clay_RenderCommandArray layout(bool rtl)
{
    CHECK(ui_widgets_begin(&widgets,UI_SHOWCASE_WIDTH,UI_SHOWCASE_HEIGHT,(UiWidgetInput){.x=-1,.y=-1})==UI_WIDGET_OK);
    ui_component_showcase(&widgets,rtl);
    Clay_RenderCommandArray commands=ui_widgets_end(&widgets);
    CHECK(widgets.error==UI_WIDGET_OK && commands.length>0);
    return commands;
}
static void save(const char *directory,unsigned dpi,bool rtl,unsigned width,unsigned height)
{
    char path[512];
    CHECK(snprintf(path,sizeof(path),"%s/showcase-%u-%s.rgba",directory,dpi,rtl?"rtl":"ltr")>0);
    FILE *file=NULL; CHECK(fopen_s(&file,path,"wb")==0 && file);
    CHECK(fwrite(pixels,4,(size_t)width*height,file)==(size_t)width*height && fclose(file)==0);
    CHECK(snprintf(path,sizeof(path),"%s/showcase-%u-%s.json",directory,dpi,rtl?"rtl":"ltr")>0);
    CHECK(fopen_s(&file,path,"wb")==0 && file);
    CHECK(fprintf(file,"{\"dpi\":%u,\"scale\":%.2f,\"width\":%u,\"height\":%u,"
        "\"logical_width\":%u,\"logical_height\":%u,\"rtl\":%s,"
        "\"renderer\":\"DX11 WARP\",\"font\":\"Segoe UI / DirectWrite fallback\","
        "\"source\":\"native component fixture; not a Spotify screenshot\"}\n",
        dpi,(double)dpi/96,width,height,UI_SHOWCASE_WIDTH,UI_SHOWCASE_HEIGHT,rtl?"true":"false")>0);
    CHECK(fclose(file)==0);
}
static void capture(const char *directory,unsigned dpi,bool rtl)
{
    float scale=(float)dpi/96;
    unsigned width=(unsigned)(UI_SHOWCASE_WIDTH*scale),height=(unsigned)(UI_SHOWCASE_HEIGHT*scale);
    CHECK(ui_renderer_resize(&renderer,width,height)==UI_RENDER_OK);
    CHECK(font_cache_reset(&fonts)==FONT_OK);
    text=(FontMeasureContext){.cache=&fonts,.config={.dpi=(uint16_t)dpi,.weight=400}};
    font_bind_clay(&text);
    /* Retain scalar advances independently of glyph-run LRU, as app_text.c
     * does. Word probes must not evict the complete strings being drawn. */
    measure_count=measure_next=0;
    Clay_SetMeasureTextFunction(measure,&text);
    UiClayBridge bridge={.custom=font_render_clay,.context=&text};
    bool ready=false;
    for (unsigned i=0;i<128;++i) {
        CHECK(font_prepare_pending(&fonts)==FONT_OK);
        Clay_RenderCommandArray commands=layout(rtl);
        /* Prepare full strings outside the allocation-free layout/render path.
         * Clay word measurements are handled by the bounded pending queue. */
        for (int32_t j=0;j<commands.length;++j) {
            Clay_RenderCommand *c=&commands.internalArray[j];
            if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
                Clay_TextRenderData *t=&c->renderData.text;
                FontConfig config=font_config_for_clay(text.config,t->fontId,t->fontSize,t->letterSpacing,t->lineHeight);
                FontHandle handle;
                CHECK(font_prepare_text(&fonts,t->stringContents,&config,&handle)==FONT_OK);
            }
        }
        CHECK(font_upload_renderer(&fonts,&renderer)==FONT_OK);
        CHECK(ui_frame_begin(&frame,width,height,scale)==UI_RENDER_OK);
        CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK);
        if (font_stats(&fonts).pending==0 && i>1) { ready=true; break; }
    }
    CHECK(ready);
    uint64_t prepared=font_stats(&fonts).prepared,misses=font_stats(&fonts).misses;
    ui_heap_begin();
    for (unsigned i=0;i<100;++i) {
        Clay_RenderCommandArray commands=layout(rtl);
        CHECK(ui_frame_begin(&frame,width,height,scale)==UI_RENDER_OK);
        CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK);
    }
    ui_heap_end();
    CHECK(font_stats(&fonts).pending==0 && font_stats(&fonts).prepared==prepared && font_stats(&fonts).misses==misses);
    const float clear[4]={0,0,0,1};
    CHECK(ui_renderer_submit(&renderer,&frame,clear)==UI_RENDER_OK);
    CHECK(ui_renderer_readback(&renderer,pixels,(size_t)width*4,sizeof(pixels))==UI_RENDER_OK);
    unsigned ink=0,green=0;
    for (size_t i=0;i<(size_t)width*height;++i) {
        const unsigned char *p=pixels+i*4;
        CHECK(p[3]==255);
        if (p[0]>100 && p[1]>100 && p[2]>100) { ++ink; }
        if (p[1]>120 && p[1]>p[0]*2 && p[1]>p[2]) { ++green; }
    }
    CHECK(ink>1000 && green>1000);
    save(directory,dpi,rtl,width,height);
}
int main(int argc,char **argv)
{
    CHECK(argc==2);
    CHECK(ui_widgets_init(&widgets,clay_memory,sizeof(clay_memory),scratch,sizeof(scratch),fail,NULL)==UI_WIDGET_OK);
    UiRendererConfig config={.width=UI_SHOWCASE_WIDTH,.height=UI_SHOWCASE_HEIGHT,.warp=true};
    UiRenderResult result=ui_renderer_create(&renderer,&config);
    if (result==UI_RENDER_UNSUPPORTED) { (void)ui_widgets_dispose(&widgets); puts("SKIP: DX11 WARP unavailable"); return 77; }
    CHECK(result==UI_RENDER_OK && font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    const unsigned dpi[]={96,120,144,192};
    for (unsigned i=0;i<4;++i) { capture(argv[1],dpi[i],false); capture(argv[1],dpi[i],true); }
    font_cache_destroy(&fonts);
    CHECK(font_stats(&fonts).com_objects==0 && ui_renderer_stats(&renderer).textures==0);
    ui_renderer_destroy(&renderer); CHECK(ui_renderer_stats(&renderer).resources==0);
    CHECK(ui_widgets_dispose(&widgets)==UI_WIDGET_OK);
    puts("Component pixels: all states, LTR/RTL, 96/120/144/192 DPI, warm allocation and resource checks passed");
    return 0;
}
