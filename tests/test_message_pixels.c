#include "message_test.h"

/* Explicit WARP opt-in: compare actual Clay/DirectWrite glyph pixels with an
 * independently configured, untrimmed run. Layout rectangles alone cannot
 * detect the original shared-ellipsis bug. Captures are renderer readbacks. */
static App app;
static FontCache fonts;
static FontMeasureContext text;
static UiRenderer renderer;
static UiRenderFrame frame;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static unsigned char pixels[2880u*1440u*4u], reference[sizeof(pixels)];
static _Alignas(16) unsigned char models[COLLECTION_MODEL_BUDGET], pages[MEMORY_BUDGET_API_PAGES];
static CollectionService *collection;

static void home(void)
{
    JsonPagePool *pool; CHECK(json_page_pool_init(pages,sizeof(pages),&pool)==JSON_OK);
    CHECK(collection_service_init(models,sizeof(models),pool,&collection)==COLLECTION_OK);
    CollectionRequest request; const CollectionPage *page;
    CHECK(collection_fetch(collection,COLLECTION_HOME,0,app.model.generation,&request)==COLLECTION_PENDING);
    FILE *file=NULL; CHECK(!fopen_s(&file,APP_FIXTURE_DIR "/home.json","rb"));
    size_t bytes=fread(request.body,1,request.capacity,file); CHECK(!ferror(file) && fgetc(file)==EOF && !fclose(file));
    CHECK(collection_complete(collection,request.ticket,COLLECTION_OK,200,bytes,&page)==COLLECTION_OK);
    app.model.page=page; app.model.logged_in=true; app.model.online=true; app.model.status=APP_CONTENT;
}

static void capture(const char *name, unsigned width, unsigned height)
{
    char path[128]; CHECK(snprintf(path,sizeof(path),"message-%s.bmp",name)>0);
    FILE *file=NULL; CHECK(!fopen_s(&file,path,"wb"));
    BITMAPFILEHEADER header={0}; header.bfType=0x4d42;
    header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER); header.bfSize=header.bfOffBits+width*height*4;
    BITMAPINFOHEADER info={sizeof(info),(LONG)width,-(LONG)height,1,32,BI_RGB,0,0,0,0,0};
    CHECK(fwrite(&header,sizeof(header),1,file)==1 && fwrite(&info,sizeof(info),1,file)==1);
    /* Renderer readback is RGBA; BMP stores BGRA. */
    for (size_t i=0;i<(size_t)width*height;++i) {
        unsigned char p[4]={pixels[4*i+2],pixels[4*i+1],pixels[4*i],255}; CHECK(fwrite(p,4,1,file)==1);
    }
    CHECK(!fclose(file)); printf("capture %s %ux%u\n",path,width,height);
}
static bool notice(const Clay_RenderCommand *c)
{
    if (c->commandType!=CLAY_RENDER_COMMAND_TYPE_TEXT || c->renderData.text.fontId!=FONT_FACE_NOTICE) { return false; }
    uintptr_t at=(uintptr_t)c->renderData.text.stringContents.chars, start=(uintptr_t)app.model.notice;
    return at>=start && at+(uint32_t)c->renderData.text.stringContents.length<=start+strlen(app.model.notice);
}
static void draw(Clay_RenderCommandArray commands, unsigned w, unsigned h, float scale)
{
    CHECK(ui_frame_begin(&frame,w,h,scale)==UI_RENDER_OK);
    UiClayBridge bridge={.custom=app_render_custom,.context=&app};
    CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK);
    const float clear[4]={0,0,0,1}; CHECK(ui_renderer_submit(&renderer,&frame,clear)==UI_RENDER_OK);
    CHECK(ui_renderer_readback(&renderer,pixels,(size_t)w*4,sizeof(pixels))==UI_RENDER_OK);
}
static void check_pixels(Clay_RenderCommandArray commands, unsigned w, unsigned h, float scale)
{
    const float clear[4]={0}; FontHandle handles[8]; unsigned count=0;
    for (int32_t i=0;i<commands.length;++i) {
        Clay_RenderCommand *c=&commands.internalArray[i]; if (!notice(c)) { continue; }
        FontConfig key={.size=14,.dpi=text.config.dpi,.weight=400,.line_height=20};
        CHECK(count<8 && font_prepare_text(&fonts,c->renderData.text.stringContents,&key,&handles[count])==FONT_OK);
        FontMetrics metrics; CHECK(font_metrics(&fonts,handles[count],&metrics)==FONT_OK && !metrics.trimmed); ++count;
    }
    CHECK(count); CHECK(font_upload_renderer(&fonts,&renderer)==FONT_OK);
    for (unsigned pass=0;pass<2;++pass) {
        CHECK(ui_frame_begin(&frame,w,h,scale)==UI_RENDER_OK); unsigned at=0;
        for (int32_t i=0;i<commands.length;++i) {
            Clay_RenderCommand *c=&commands.internalArray[i]; if (!notice(c)) { continue; }
            if (!pass) { CHECK(font_render_clay(&text,&frame,c)==UI_RENDER_OK); }
            else {
                Clay_Color t=c->renderData.text.textColor; float color[4]={t.r/255,t.g/255,t.b/255,t.a/255};
                CHECK(font_render_text(&fonts,handles[at],&frame,c->boundingBox.x,c->boundingBox.y,color)==FONT_OK);
            }
            ++at;
        }
        CHECK(ui_renderer_submit(&renderer,&frame,clear)==UI_RENDER_OK);
        CHECK(ui_renderer_readback(&renderer,pass ? reference : pixels,(size_t)w*4,sizeof(pixels))==UI_RENDER_OK);
    }
    CHECK(!memcmp(pixels,reference,(size_t)w*h*4));
    unsigned ink=0; for (size_t i=0;i<(size_t)w*h;++i) { if (pixels[4*i+3]) { ++ink; } }
    CHECK(ink>100); printf("notice glyph pixels match untrimmed reference: %u pixels\n",ink);
}
static void visual(float width, float height, unsigned dpi, const char *name, const char *message)
{
    float scale=(float)dpi/96; unsigned w=(unsigned)(width*scale), h=(unsigned)(height*scale);
    CHECK(font_cache_reset(&fonts)==FONT_OK); text.config.dpi=(uint16_t)dpi;
    CHECK(ui_renderer_resize(&renderer,w,h)==UI_RENDER_OK);
    CHECK(app_set_notice(&app,APP_WARNING,message_string(message))==APP_OK);
    Clay_RenderCommandArray commands={0};
    for (unsigned i=0;i<64;++i) {
        CHECK(font_prepare_pending(&fonts)==FONT_OK); CHECK(font_upload_renderer(&fonts,&renderer)==FONT_OK);
        commands=app_layout(&app,(float)w,(float)h,(float)dpi,(UiWidgetInput){.x=-1,.y=-1});
        CHECK(commands.length && app.widgets.error==UI_WIDGET_OK); draw(commands,w,h,scale);
        if (!font_stats(&fonts).pending) { break; }
    }
    CHECK(!font_stats(&fonts).pending); capture(name,w,h); check_pixels(commands,w,h,scale);
    if (!strcmp(name,"after-default")) {
        /* Faithful reproduction of the old single-line draw policy, in the
         * same fixture shell. This is labeled as a reproduction, not a capture
         * of a signed-in account or of the historical executable. */
        bool first=true;
        for (int32_t i=0;i<commands.length;++i) {
            Clay_RenderCommand *c=&commands.internalArray[i]; if (!notice(c)) { continue; }
            c->renderData.text.fontId=0;
            c->renderData.text.stringContents=(Clay_StringSlice){first ? sizeof(HOME_NOTICE)-1 : 0,HOME_NOTICE,HOME_NOTICE};
            first=false;
            FontConfig key=font_clay_config(&text,0,14,0,20); FontHandle handle;
            CHECK(font_prepare_text(&fonts,c->renderData.text.stringContents,&key,&handle)==FONT_OK);
            FontMetrics metrics; CHECK(font_metrics(&fonts,handle,&metrics)==FONT_OK);
            CHECK(!c->renderData.text.stringContents.length || metrics.trimmed);
        }
        CHECK(font_upload_renderer(&fonts,&renderer)==FONT_OK); draw(commands,w,h,scale); capture("before-reproduction",w,h);
    }
}
int main(void)
{
    UiRendererConfig config={.width=1100,.height=720,.warp=true};
    UiRenderResult result=ui_renderer_create(&renderer,&config);
    if (result==UI_RENDER_UNSUPPORTED) { puts("SKIP: WARP unavailable"); return 77; } CHECK(result==UI_RENDER_OK);
    AppServices services={0}; CHECK(app_init(&app,&services,NULL,clay,sizeof(clay),scratch,sizeof(scratch))==APP_OK);
    home();
    CHECK(font_cache_create(&fonts,NULL,NULL)==FONT_OK);
    text=(FontMeasureContext){.cache=&fonts,.config={.dpi=96,.weight=400,.max_width=260,.ellipsis=true}}; app_bind_fonts(&app,&text);
    visual(1100,720,96,"after-default",HOME_NOTICE);
    visual(400,720,96,"after-narrow",HOME_NOTICE);
    visual(1440,720,192,"after-wide-192",HOME_NOTICE);
    visual(400,720,144,"rtl-144",RTL_NOTICE);
    visual(400,720,120,"multiline-120","Caf\xc3\xa9 e\xcc\x81 \xe4\xb8\xad\xe6\x96\x87\nRetry this action.");
    font_cache_destroy(&fonts); app_dispose(&app); CHECK(collection_reset(collection,app.model.generation+2)==COLLECTION_OK);
    CHECK(!collection_resident_pages(collection)); CHECK(ui_renderer_debug_report(&renderer));
    puts("actual DX11/DirectWrite notice glyphs and resource teardown passed"); return 0;
}
