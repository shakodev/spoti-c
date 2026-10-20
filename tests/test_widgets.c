#include "ui/widgets.h"
#include "ui/renderer_clay.h"
#include "support/test_check.h"
#include "support/ui_heap_watch.h"
#include <math.h>
#include <string.h>

static UiWidgets ui;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static UiRenderFrame frame;
static unsigned errors;
static void error(void *context, UiWidgetResult result) { CHECK(context == &errors); CHECK(result != UI_WIDGET_OK); ++errors; }
static void text_editing(void)
{
    UiTextInput t = {0};
    const char unicode[] = "A\xc3\xa9\xf0\x9f\x98\x80Z";
    CHECK(ui_text_set(&t,unicode,8) == UI_WIDGET_OK);
    CHECK(ui_text_select(&t,1,3) == UI_WIDGET_OK);
    CHECK(ui_text_key(&t,UI_TEXT_RIGHT,false) == UI_WIDGET_OK && t.caret == 3);
    CHECK(ui_text_key(&t,UI_TEXT_RIGHT,true) == UI_WIDGET_OK && t.anchor == 3 && t.caret == 7);
    CHECK(ui_text_key(&t,UI_TEXT_BACKSPACE,false) == UI_WIDGET_OK && !strcmp(t.text,"A\xc3\xa9Z"));
    CHECK(ui_text_key(&t,UI_TEXT_BACKSPACE,false) == UI_WIDGET_OK && !strcmp(t.text,"AZ"));
    CHECK(ui_text_key(&t,UI_TEXT_DELETE,false) == UI_WIDGET_OK && !strcmp(t.text,"A"));
    CHECK(ui_text_key(&t,UI_TEXT_HOME,false) == UI_WIDGET_OK && t.caret == 0);
    CHECK(ui_text_key(&t,UI_TEXT_LEFT,false) == UI_WIDGET_OK && t.caret == 0);
    CHECK(ui_text_key(&t,UI_TEXT_DELETE,false) == UI_WIDGET_OK && t.length == 0);
    CHECK(ui_text_replace(&t,unicode,8) == UI_WIDGET_OK);
    CHECK(ui_text_select(&t,2,3) == UI_WIDGET_INVALID && t.caret == 8);
    CHECK(ui_text_select(&t,7,1) == UI_WIDGET_OK);
    CHECK(ui_text_replace(&t,t.text + 1,2) == UI_WIDGET_OK && !strcmp(t.text,"A\xc3\xa9Z"));
    const char *invalid[] = {"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf0\x9f","\x80"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        UiTextInput before = t;
        CHECK(ui_text_replace(&t,invalid[i],(uint32_t)strlen(invalid[i])) == UI_WIDGET_INVALID);
        CHECK(!memcmp(&t,&before,sizeof(t)));
    }
    CHECK(ui_text_set(&t,"a\0b",3) == UI_WIDGET_INVALID);
    char full[UI_TEXT_BYTES]; memset(full,'x',sizeof(full));
    CHECK(ui_text_set(&t,full,sizeof(full) - 1) == UI_WIDGET_OK);
    UiTextInput before = t;
    CHECK(ui_text_replace(&t,"x",1) == UI_WIDGET_FULL && !memcmp(&t,&before,sizeof(t)));
    CHECK(ui_text_key(&t,UI_TEXT_SELECT_ALL,false) == UI_WIDGET_OK);
    CHECK(ui_text_replace(&t,"small",5) == UI_WIDGET_OK && !strcmp(t.text,"small"));
    CHECK(ui_text_key(&t,UI_TEXT_END,true) == UI_WIDGET_OK && t.caret == 5);
}
typedef struct Clipboard { char data[UI_TEXT_BYTES]; uint32_t length; UiWidgetResult result; unsigned writes; } Clipboard;
static UiWidgetResult read_clip(void *context, char *out, uint32_t capacity, uint32_t *length)
{
    Clipboard *c = context; CHECK(c->length < capacity);
    memcpy(out,c->data,c->length); *length = c->length; return c->result;
}
static UiWidgetResult write_clip(void *context, const char *s, uint32_t length)
{
    Clipboard *c = context; ++c->writes;
    if (c->result != UI_WIDGET_OK) { return c->result; }
    memcpy(c->data,s,length); c->length = length; return UI_WIDGET_OK;
}
static void clipboard_ime(void)
{
    UiTextInput t = {0}; Clipboard c = {0}; UiClipboard clip = {read_clip,write_clip,&c};
    CHECK(ui_text_set(&t,"abcdef",6) == UI_WIDGET_OK);
    CHECK(ui_text_select(&t,4,1) == UI_WIDGET_OK);
    CHECK(ui_text_clipboard(&t,&clip,false,false) == UI_WIDGET_OK && c.length == 3 && !memcmp(c.data,"bcd",3));
    c.result = UI_WIDGET_OS_ERROR; UiTextInput before = t;
    CHECK(ui_text_clipboard(&t,&clip,false,true) == UI_WIDGET_OS_ERROR && !memcmp(&t,&before,sizeof(t)));
    c.result = UI_WIDGET_OK;
    CHECK(ui_text_clipboard(&t,&clip,false,true) == UI_WIDGET_OK && !strcmp(t.text,"aef"));
    CHECK(ui_text_clipboard(&t,&clip,true,false) == UI_WIDGET_OK && !strcmp(t.text,"abcdef"));
    CHECK(ui_text_select(&t,1,4) == UI_WIDGET_OK);
    const uint16_t preedit[] = {0x65e5,0xd83d,0xde00};
    CHECK(ui_text_ime_utf16(&t,preedit,3,2,false) == UI_WIDGET_INVALID); /* split surrogate */
    CHECK(ui_text_ime_utf16(&t,preedit,3,3,false) == UI_WIDGET_OK);
    CHECK(t.composing && t.composition_length == 7 && t.composition_caret == 7 && !strcmp(t.text,"abcdef"));
    CHECK(ui_text_key(&t,UI_TEXT_LEFT,false) == UI_WIDGET_CANCEL);
    CHECK(ui_text_clipboard(&t,&clip,true,false) == UI_WIDGET_CANCEL);
    ui_text_composition_cancel(&t); CHECK(!t.composing && !strcmp(t.text,"abcdef") && t.anchor == 1 && t.caret == 4);
    CHECK(ui_text_ime_utf16(&t,preedit,3,1,false) == UI_WIDGET_OK && t.composition_caret == 3);
    CHECK(ui_text_ime_utf16(&t,preedit,3,3,true) == UI_WIDGET_OK);
    CHECK(!strcmp(t.text,"a\xe6\x97\xa5\xf0\x9f\x98\x80" "ef") && !t.composing);
    const uint16_t bad[] = {0xd800};
    CHECK(ui_text_ime_utf16(&t,bad,1,1,true) == UI_WIDGET_INVALID);
    before = t; char large[UI_COMPOSITION_BYTES]; memset(large,'a',sizeof(large));
    CHECK(ui_text_compose(&t,large,sizeof(large),0) == UI_WIDGET_FULL && !memcmp(&t,&before,sizeof(t)));
    CHECK(ui_text_compose(&t,"\xc3\xa9",2,1) == UI_WIDGET_INVALID);
    char full[UI_TEXT_BYTES]; memset(full,'a',sizeof(full));
    CHECK(ui_text_set(&t,full,sizeof(full) - 1) == UI_WIDGET_OK);
    CHECK(ui_text_compose(&t,"x",1,1) == UI_WIDGET_OK); before = t;
    CHECK(ui_text_composition_commit(&t,"x",1) == UI_WIDGET_FULL && !memcmp(&t,&before,sizeof(t)));
    CHECK(ui_text_set(&t,"abcdef",6) == UI_WIDGET_OK);
    ui_text_focus(&t,true,100); CHECK(ui_text_tick(&t,599) == 600 && t.caret_visible);
    CHECK(ui_text_tick(&t,600) == 1100 && !t.caret_visible);
    CHECK(ui_text_compose(&t,"x",1,1) == UI_WIDGET_OK && ui_text_tick(&t,700) == UI_CARET_NEVER);
    ui_text_focus(&t,false,700); CHECK(!t.composing && ui_text_tick(&t,10000) == UI_CARET_NEVER);
    /* Full-buffer paste is transactional, including the original selection. */
    memset(c.data,'x',sizeof(c.data)); c.length = UI_TEXT_BYTES - 1; before = t;
    CHECK(ui_text_clipboard(&t,&clip,true,false) == UI_WIDGET_FULL && !memcmp(&t,&before,sizeof(t)));
}
static void seeks(void)
{
    UiSeekSlider s = {.value=0.4}; double value;
    CHECK(ui_seek_begin(&s,-5) == UI_WIDGET_OK && s.value == 0);
    for (unsigned i = 0; i < 1000; ++i) { CHECK(ui_seek_move(&s,i / 999.0) == UI_WIDGET_OK); }
    CHECK(ui_seek_take_preview(&s,&value) && value == 1 && !ui_seek_take_preview(&s,&value));
    CHECK(!ui_seek_take_commit(&s,&value));
    CHECK(ui_seek_move(&s,NAN) == UI_WIDGET_INVALID && s.value == 1);
    CHECK(ui_seek_commit(&s) == UI_WIDGET_OK && ui_seek_take_commit(&s,&value) && value == 1);
    CHECK(!ui_seek_take_commit(&s,&value) && !ui_seek_take_preview(&s,&value));
    CHECK(ui_seek_begin(&s,0) == UI_WIDGET_OK && ui_seek_cancel(&s) == UI_WIDGET_CANCEL && s.value == 1);
    CHECK(!ui_seek_take_commit(&s,&value) && !ui_seek_take_preview(&s,&value));
    CHECK(ui_seek_key(&s,9) == UI_WIDGET_OK && ui_seek_take_commit(&s,&value) && value == 1);
    CHECK(ui_seek_key(&s,-1) == UI_WIDGET_OK && ui_seek_take_commit(&s,&value) && value == 0);
    CHECK(ui_seek_key(&s,INFINITY) == UI_WIDGET_INVALID);
}
static UiRenderResult draw_text(void *context, UiRenderFrame *f, const Clay_RenderCommand *command)
{
    (void)context; QuadInstance q = {.box={command->boundingBox.x,command->boundingBox.y,
        command->boundingBox.width,command->boundingBox.height},.color={1,1,1,1}};
    return ui_frame_quad(f,&q,0);
}
static bool button_frame(UiWidgetInput input, bool disabled)
{
    CHECK(ui_widgets_begin(&ui,800,600,input) == UI_WIDGET_OK);
    bool hit = ui_button(&ui,CLAY_ID("button"),CLAY_STRING("Play"),100,30,disabled);
    ui_seek_slider(&ui,CLAY_ID("seek"),&ui.seeks[0],200);
    ui_text_input(&ui,CLAY_ID("text"),&ui.texts[0],200);
    Clay_RenderCommandArray commands = ui_widgets_end(&ui);
    CHECK(ui.error == UI_WIDGET_OK && commands.length > 0);
    CHECK(ui_frame_begin(&frame,1000,750,1.25f) == UI_RENDER_OK);
    UiClayBridge bridge = {.custom=draw_text};
    CHECK(ui_frame_clay(&frame,commands,&bridge) == UI_RENDER_OK && frame.quad_count > 0);
    return hit;
}
static void layout(void)
{
    CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),error,&errors) == UI_WIDGET_OK);
    CHECK(Clay_MinMemorySize() <= UI_CLAY_BYTES);
    CHECK(ui_text_set(&ui.texts[0],"Hello",5) == UI_WIDGET_OK);
    CHECK(!button_frame((UiWidgetInput){.x=5,.y=5},false));
    CHECK(!button_frame((UiWidgetInput){.x=5,.y=5,.down=true,.pressed=true},false));
    CHECK(ui.active == CLAY_ID("button").id);
    CHECK(!button_frame((UiWidgetInput){.x=500,.y=500,.released=true},false));
    CHECK(!button_frame((UiWidgetInput){.x=5,.y=5,.down=true,.pressed=true},false));
    CHECK(button_frame((UiWidgetInput){.x=5,.y=5,.released=true},false));
    CHECK(button_frame((UiWidgetInput){.activate=true},false));
    CHECK(!button_frame((UiWidgetInput){.activate=true},true));
    /* Native pointer capture: preview is coalesced; release outside clamps and
     * commits exactly once, while cancellation restores the starting value. */
    CHECK(!button_frame((UiWidgetInput){.x=150,.y=5,.pressed=true,.down=true},false));
    CHECK(ui.seeks[0].dragging && fabs(ui.seeks[0].value - 0.25) < 1e-6);
    CHECK(!button_frame((UiWidgetInput){.x=1000,.y=100,.released=true},false));
    double seek;
    CHECK(ui_seek_take_commit(&ui.seeks[0],&seek) && seek == 1);
    CHECK(!ui_seek_take_commit(&ui.seeks[0],&seek));
    CHECK(!button_frame((UiWidgetInput){.x=100,.y=5,.pressed=true,.down=true},false));
    CHECK(!button_frame((UiWidgetInput){.cancel=true},false));
    CHECK(ui.seeks[0].value == 1 && !ui.seeks[0].dragging && !ui_seek_take_commit(&ui.seeks[0],&seek));
    ui_heap_begin();
    for (unsigned i = 0; i < 2000; ++i) {
        CHECK(!button_frame((UiWidgetInput){.x=(float)(i % 120),.y=5},false));
    }
    ui_heap_end(); CHECK(errors == 0);
    CHECK(ui_seek_begin(&ui.seeks[1],0.8) == UI_WIDGET_OK);
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){.released=true}) == UI_WIDGET_OK);
    (void)ui_widgets_end(&ui); /* Captured slider was culled. */
    CHECK(!ui.seeks[1].dragging && ui.seeks[1].value == 0);
    CHECK(!ui_seek_take_commit(&ui.seeks[1],&seek));
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    CHECK(ui_widgets_scratch(&ui,7,64) != NULL);
    CHECK(ui_widgets_scratch(&ui,UI_SCRATCH_BYTES,1) == NULL);
    CHECK(ui.error == UI_WIDGET_FULL && errors == 1);
    Clay_RenderCommandArray commands = ui_widgets_end(&ui);
    CHECK(commands.length >= 2 && ui.scratch_used == 0 && !ui.in_frame);
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    CHECK(ui_widgets_scratch(&ui,UI_SCRATCH_BYTES,1) != NULL);
    (void)ui_widgets_end(&ui);
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    for (unsigned i = 0; i < 450; ++i) {
        (void)ui_button(&ui,CLAY_IDI("many",i),CLAY_STRING("b"),1,1,false);
    }
    commands = ui_widgets_end(&ui); CHECK(ui.error == UI_WIDGET_FULL && commands.length >= 2);
    /* Failed initialization reports exhaustion before any Clay access. */
    static UiWidgets failed;
    CHECK(ui_widgets_init(&failed,clay,64,scratch,sizeof(scratch),error,&errors) == UI_WIDGET_FULL);
    CHECK(errors == 3);
    printf("widget state=%zu bytes, Clay minimum=%u bytes, scratch=%u bytes\n",
        sizeof(ui),Clay_MinMemorySize(),UI_SCRATCH_BYTES);
    CHECK(ui_widgets_dispose(&ui) == UI_WIDGET_OK && Clay_GetCurrentContext() == NULL);
    puts("2000 widget layout/measurement/render frames; Debug allocation hooks and bounded visible failures passed");
}
static bool track_frame(UiWidgetInput input, bool disabled, bool current)
{
    CHECK(ui_widgets_begin(&ui,800,600,input)==UI_WIDGET_OK);
    bool hit=ui_track_button(&ui,CLAY_ID("track"),CLAY_STRING("Track"),200,disabled,current);
    Clay_RenderCommandArray commands=ui_widgets_end(&ui); CHECK(ui.error==UI_WIDGET_OK);
    bool text=false;
    for (int32_t i=0;i<commands.length;++i) {
        Clay_RenderCommand *c=Clay_RenderCommandArray_Get(&commands,i);
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
            text=true;
            CHECK(c->renderData.text.textColor.g==(disabled ? 145 : current ? 215 : 245));
        }
    }
    CHECK(text); return hit;
}
static void track_button(void)
{
    errors=0; CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),error,&errors)==UI_WIDGET_OK);
    CHECK(!track_frame((UiWidgetInput){.x=5,.y=5},false,true));
    CHECK(!track_frame((UiWidgetInput){.x=5,.y=5,.down=true,.pressed=true},false,true));
    CHECK(track_frame((UiWidgetInput){.x=5,.y=5,.released=true},false,true));
    CHECK(track_frame((UiWidgetInput){.activate=true},false,false));
    CHECK(!track_frame((UiWidgetInput){.activate=true},true,true));
    ui_heap_begin();
    for (unsigned i=0;i<1000;++i) { CHECK(!track_frame((UiWidgetInput){0},false,i%2!=0)); }
    ui_heap_end(); CHECK(!errors && ui_widgets_dispose(&ui)==UI_WIDGET_OK);
}
int main(void)
{
    text_editing(); clipboard_ime(); seeks(); layout(); track_button();
    puts("ui_widgets: Unicode selection, IME, clipboard, seeks, focus/capture, arenas and allocation checks passed");
    return 0;
}
