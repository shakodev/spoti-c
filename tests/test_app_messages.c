#include "message_test.h"
#include "support/ui_heap_watch.h"
#include <math.h>

static App app;
static FontCache fonts;
static FontMeasureContext text;
static UiRenderFrame frame;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static Clay_RenderCommandArray layout(float w, float h, unsigned dpi)
{
    Clay_RenderCommandArray commands=app_layout(&app,w*(float)dpi/96,h*(float)dpi/96,(float)dpi,(UiWidgetInput){.x=-1,.y=-1});
    CHECK(commands.length && app.widgets.error==UI_WIDGET_OK);
    return commands;
}
static Clay_RenderCommandArray settle(float w, float h, unsigned dpi)
{
    if (text.config.dpi!=dpi) { CHECK(font_cache_reset(&fonts)==FONT_OK); text.config.dpi=(uint16_t)dpi; }
    Clay_RenderCommandArray commands={0};
    for (unsigned i=0;i<64;++i) {
        CHECK(font_prepare_pending(&fonts)==FONT_OK); CHECK(font_atlas_commit(&fonts,1)==FONT_OK);
        commands=layout(w,h,dpi);
        CHECK(ui_frame_begin(&frame,(uint32_t)(w*(float)dpi/96),(uint32_t)(h*(float)dpi/96),(float)dpi/96)==UI_RENDER_OK);
        UiClayBridge bridge={.custom=app_render_custom,.context=&app};
        CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK);
        if (!font_stats(&fonts).pending) { return commands; }
    }
    CHECK(!"status text failed to settle"); return commands;
}
static void regression(void)
{
    CHECK(app_set_notice(&app,APP_WARNING,message_string(HOME_NOTICE))==APP_OK);
    const float widths[]={400,760,1100,1440}; const unsigned dpis[]={96,120,144,192,384};
    float narrow_height=0, wide_height=0;
    for (unsigned d=0;d<5;++d) {
        for (unsigned w=0;w<4;++w) {
            Clay_RenderCommandArray commands=settle(widths[w],720,dpis[d]);
            bool covered[sizeof(HOME_NOTICE)]={0}; float first=-1, last=0;
            Clay_ElementData pane=Clay_GetElementData(CLAY_ID("AppMain")); CHECK(pane.found);
            Clay_ElementData read=Clay_GetElementData(CLAY_ID("AppReadMessages")); CHECK(read.found);
            CHECK(read.boundingBox.x>=pane.boundingBox.x && read.boundingBox.x+read.boundingBox.width<=pane.boundingBox.x+pane.boundingBox.width);
            for (int32_t i=0;i<commands.length;++i) {
                Clay_RenderCommand *c=&commands.internalArray[i];
                if (c->commandType!=CLAY_RENDER_COMMAND_TYPE_TEXT || c->renderData.text.fontId!=FONT_FACE_NOTICE) { continue; }
                Clay_StringSlice s=c->renderData.text.stringContents;
                uintptr_t at=(uintptr_t)s.chars, start=(uintptr_t)app.model.notice;
                if (at<start || at+(uint32_t)s.length>start+strlen(app.model.notice)) { continue; }
                if (!(c->boundingBox.x>=pane.boundingBox.x && c->boundingBox.x+c->boundingBox.width<=pane.boundingBox.x+pane.boundingBox.width+0.1f)) {
                    fprintf(stderr,"pane %.2f %.2f text %.2f %.2f width %.0f dpi %u: %.*s\n",(double)pane.boundingBox.x,
                        (double)pane.boundingBox.width,(double)c->boundingBox.x,(double)c->boundingBox.width,
                        (double)widths[w],dpis[d],s.length,s.chars);
                    CHECK(!"notice escaped pane");
                }
                FontConfig key=font_clay_config(&text,FONT_FACE_NOTICE,14,0,20); FontHandle handle; FontMetrics metrics;
                CHECK(font_lookup(&fonts,s,&key,&handle)==FONT_OK && font_metrics(&fonts,handle,&metrics)==FONT_OK);
                CHECK(!metrics.trimmed && metrics.glyphs>0 && metrics.width<=c->boundingBox.width+0.1f);
                for (int32_t j=0;j<s.length;++j) { covered[at-start+(uint32_t)j]=true; }
                if (first<0) { first=c->boundingBox.y; } last=c->boundingBox.y+c->boundingBox.height;
            }
            for (unsigned i=0;i<sizeof(HOME_NOTICE)-1;++i) { CHECK(HOME_NOTICE[i]==' ' || covered[i]); }
            if (!d && !w) { narrow_height=last-first; }
            if (!d && w==3) { wide_height=last-first; }
            CHECK(strstr(app.messages.text,HOME_NOTICE));
        }
    }
    CHECK(narrow_height>wide_height && narrow_height<=60);
}
static void bounds(void)
{
    char maximum[APP_NOTICE_BYTES]; memset(maximum,'W',sizeof(maximum)-1); maximum[sizeof(maximum)-1]=0;
    const char *cases[]={"Ready.","First line\nSecond line\nThird line: retry.",
        "Caf\xc3\xa9 e\xcc\x81 \xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x98\x80. Try again.",RTL_NOTICE,maximum};
    for (unsigned i=0;i<5;++i) {
        CHECK(app_set_notice(&app,(AppSeverity)(i%3),message_string(cases[i]))==APP_OK);
        (void)settle(400,320,96);
        CHECK(strstr(app.messages.text,i==1 ? "First line\r\nSecond line\r\nThird line: retry." : cases[i]));
        app.widgets.focus=CLAY_ID("AppBack").id;
        bool found=false;
        for (unsigned k=0;k<APP_CONTROLS;++k) {
            CHECK(app_key(&app,APP_KEY_TAB,false)==APP_OK);
            if (app.widgets.focus==CLAY_ID("AppReadMessages").id) { found=true; break; }
        }
        CHECK(found && app_key(&app,APP_KEY_ENTER,false)==APP_OK && app_update(&app,0)==APP_OK);
        CHECK(app.messages.requested && !strcmp(app.messages.details,app.messages.text));
        app.messages.requested=false;
        Clay_ElementData body=Clay_GetElementData(CLAY_ID("AppBody"));
        Clay_ElementData bar=Clay_GetElementData(CLAY_ID("AppNowPlaying"));
        CHECK(bar.boundingBox.y>=body.boundingBox.y+body.boundingBox.height);
    }
    char over[APP_NOTICE_BYTES+1]; memset(over,'A',sizeof(over));
    CHECK(app_set_notice(&app,APP_WARNING,(Clay_String){false,sizeof(over),over})==APP_FULL);
    CHECK(strstr(app.model.notice,"1023-byte limit") && strstr(app.model.notice,"Retry") && app.model.notice_severity==APP_FAILURE);
    CHECK(app_set_notice(&app,APP_WARNING,message_string("bad\xff"))==APP_INVALID);
    CHECK(strstr(app.model.notice,"invalid UTF-8"));
    CHECK(app_set_notice(&app,APP_WARNING,(Clay_String){false,3,"a\0b"})==APP_INVALID);
    CHECK(app_set_notice(&app,APP_WARNING,(Clay_String){false,-1,NULL})==APP_INVALID);
    /* Transcript overflow remains explicit, terminated, and safe to open. */
    CHECK(ui_widgets_begin(&app.widgets,1100,720,(UiWidgetInput){0})==UI_WIDGET_OK);
    app.messages.length=0; app.messages.text[0]=0; app.messages.full=false;
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_GROW()},.layoutDirection=CLAY_TOP_TO_BOTTOM}}) {
        for (unsigned i=0;i<12;++i) { app_message(&app,message_string(maximum),APP_WARNING); }
    }
    (void)ui_widgets_end(&app.widgets);
    CHECK(app.messages.full && app.messages.length<APP_MESSAGE_BYTES && strstr(app.messages.text,"Additional messages exceed"));
    CHECK(app_messages_request(&app)==APP_OK && strlen(app.messages.details)==app.messages.length);
}
static void idle(void)
{
    CHECK(app_set_notice(&app,APP_WARNING,message_string(HOME_NOTICE))==APP_OK);
    (void)settle(1100,720,96); uint64_t prepared=font_stats(&fonts).prepared;
    ui_heap_begin();
    for (unsigned i=0;i<1000;++i) {
        Clay_RenderCommandArray commands=layout(1100,720,96);
        CHECK(ui_frame_begin(&frame,1100,720,1)==UI_RENDER_OK);
        UiClayBridge bridge={.custom=app_render_custom,.context=&app};
        CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK);
        CHECK(font_stats(&fonts).pending==0 && font_stats(&fonts).prepared==prepared);
    }
    ui_heap_end();
}
int main(void)
{
    AppServices services={0}; CHECK(app_init(&app,&services,NULL,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    app.model.location.screen=APP_SETTINGS;
    CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    text=(FontMeasureContext){.cache=&fonts,.config={.dpi=96,.weight=400,.max_width=260,.ellipsis=true}};
    app_bind_fonts(&app,&text);
    regression(); bounds(); idle();
    font_cache_destroy(&fonts); app_dispose(&app);
    puts("messages: exact fallback, UTF-8/RTL/tokens, bounds, DPI, keyboard details, overflow and idle passed"); return 0;
}
