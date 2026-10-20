#include "ui/icons.h"
#include "ui/renderer_clay.h"
#include "support/test_check.h"
#include "support/ui_heap_watch.h"
#include <math.h>
#include <string.h>

static UiRenderFrame frame;
static UiWidgets ui;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static unsigned errors, custom_calls;
static const float white[4] = {1,1,1,1};

static void error(void *context, UiWidgetResult result)
{
    CHECK(context == &errors && result != UI_WIDGET_OK); ++errors;
}
static void begin(float scale)
{
    CHECK(ui_frame_begin(&frame,800,600,scale) == UI_RENDER_OK);
}
static bool directional(UiIcon icon)
{
    return icon == UI_ICON_CHEVRON_LEFT || icon == UI_ICON_CHEVRON_RIGHT;
}
static void geometry(void)
{
    const float scales[] = {1,1.25f,1.5f,2};
    const float sizes[] = {12,16,24,32,40,48,64,88};
    for (unsigned dpi = 0; dpi < 4; ++dpi) {
        for (unsigned size = 0; size < 8; ++size) {
            begin(scales[dpi]);
            CHECK(ui_frame_clip_push(&frame,(UiRect){2,3,140,150},true,true) == UI_RENDER_OK);
            for (UiIcon icon = UI_ICON_HOME; icon < UI_ICON_COUNT; ++icon) {
                UiRect bounds = {10,20,sizes[size]+10,sizes[size]};
                CHECK(ui_icon_draw(&frame,icon,bounds,white,false) == UI_RENDER_OK);
                QuadInstance ltr = frame.quads[frame.quad_count-1];
                CHECK(ltr.box[0] == 15 && ltr.box[1] == 20);
                CHECK(ltr.box[2] == sizes[size] && ltr.box[3] == sizes[size]);
                CHECK(ltr.effect[0] == UI_QUAD_ICON && ltr.effect[1] == icon);
                CHECK(ltr.effect[2] == 0 && frame.batches[0].texture == 0);
                CHECK(ui_icon_draw(&frame,icon,bounds,white,true) == UI_RENDER_OK);
                QuadInstance rtl = frame.quads[frame.quad_count-1];
                CHECK(rtl.effect[2] == (directional(icon) ? 1 : 0));
                rtl.effect[2] = 0;
                CHECK(!memcmp(&ltr,&rtl,sizeof(ltr)));
            }
            CHECK(frame.quad_count == 2*(UI_ICON_COUNT-1) && frame.batch_count == 1);
            CHECK(frame.batches[0].scissor[0] == (int32_t)floorf(2*scales[dpi]));
            CHECK(frame.batches[0].scissor[3] == (int32_t)ceilf(153*scales[dpi]));
            CHECK(ui_frame_clip_pop(&frame) == UI_RENDER_OK);
        }
    }
    begin(1);
    CHECK(ui_icon_draw(&frame,UI_ICON_PLAY,(UiRect){5,9,24,40},white,false) == UI_RENDER_OK);
    CHECK(frame.quads[0].box[0] == 5 && frame.quads[0].box[1] == 17);
    CHECK(ui_icon_draw(&frame,UI_ICON_NONE,(UiRect){0,0,24,24},white,false) == UI_RENDER_OK);
    CHECK(frame.quad_count == 1);
}
static void invalid_arguments(void)
{
    UiRect valid = {0,0,24,24};
    CHECK(ui_icon_draw(NULL,UI_ICON_HOME,valid,white,false) == UI_RENDER_INVALID);
    const UiIcon invalid_icons[] = {(UiIcon)-1,UI_ICON_COUNT,(UiIcon)1000};
    for (unsigned i = 0; i < 3; ++i) {
        begin(1);
        CHECK(ui_icon_draw(&frame,invalid_icons[i],valid,white,false) == UI_RENDER_INVALID);
        CHECK(ui_icon_draw(&frame,UI_ICON_HOME,valid,white,false) == UI_RENDER_INVALID);
        CHECK(frame.quad_count == 0);
    }
    const UiRect invalid_bounds[] = {
        {NAN,0,24,24},{0,INFINITY,24,24},{0,0,-1,24},{0,0,24,0},
        {0,0,INFINITY,24},{0,0,24,NAN},{1000001,0,24,24}
    };
    for (unsigned i = 0; i < sizeof(invalid_bounds)/sizeof(invalid_bounds[0]); ++i) {
        begin(1);
        CHECK(ui_icon_draw(&frame,UI_ICON_HOME,invalid_bounds[i],white,false) == UI_RENDER_INVALID);
    }
    const float invalid_colors[][4] = {{NAN,1,1,1},{1,-0.1f,1,1},{1,1,1.1f,1},{1,1,1,INFINITY}};
    for (unsigned i = 0; i < 4; ++i) {
        begin(1);
        CHECK(ui_icon_draw(&frame,UI_ICON_NONE,valid,invalid_colors[i],false) == UI_RENDER_INVALID);
    }
    begin(1);
    CHECK(ui_icon_draw(&frame,UI_ICON_HOME,valid,NULL,false) == UI_RENDER_INVALID);
    begin(1);
    CHECK(ui_icon_draw(&frame,UI_ICON_PLAY,valid,white,false) == UI_RENDER_OK);
    QuadInstance good = frame.quads[0], bad;
    const float invalid_effects[][4] = {
        {UI_QUAD_ICON,0,0,0},{UI_QUAD_ICON,UI_ICON_COUNT,0,0},
        {UI_QUAD_ICON,1.5f,0,0},{UI_QUAD_ICON,UI_ICON_PLAY,1,0},
        {UI_QUAD_ICON,UI_ICON_CHEVRON_LEFT,2,0},{UI_QUAD_ICON,UI_ICON_PLAY,0,1}
    };
    for (unsigned i = 0; i < sizeof(invalid_effects)/sizeof(invalid_effects[0]); ++i) {
        begin(1); bad = good; memcpy(bad.effect,invalid_effects[i],sizeof(bad.effect));
        CHECK(ui_frame_quad(&frame,&bad,0) == UI_RENDER_INVALID && !frame.quad_count);
    }
    begin(1); CHECK(ui_frame_quad(&frame,&good,1) == UI_RENDER_INVALID);
    begin(1); bad = good; bad.box[3] = 12;
    CHECK(ui_frame_quad(&frame,&bad,0) == UI_RENDER_INVALID);
    begin(1);
    for (unsigned i = 0; i < UI_MAX_QUADS; ++i) {
        CHECK(ui_icon_draw(&frame,UI_ICON_CHECK,valid,white,false) == UI_RENDER_OK);
    }
    CHECK(ui_icon_draw(&frame,UI_ICON_CHECK,valid,white,false) == UI_RENDER_FULL);
    CHECK(frame.quad_count == UI_MAX_QUADS && frame.batch_count == 1);
}
static UiRenderResult custom(void *context, UiRenderFrame *f, const Clay_RenderCommand *command)
{
    (void)context; (void)f;
    CHECK(command->renderData.custom.customData == (void *)(uintptr_t)1);
    ++custom_calls; return UI_RENDER_OK;
}
static Clay_RenderCommandArray layout_icons(void)
{
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    for (UiIcon icon = UI_ICON_HOME; icon < UI_ICON_COUNT; ++icon) {
        ui_icon(&ui,CLAY_IDI("icon",(uint32_t)icon),icon,24,(Clay_Color){255,128,0,255},true);
    }
    CHECK(ui.scratch_used > 0 && ui.scratch_used < 1024);
    Clay_RenderCommandArray commands = ui_widgets_end(&ui);
    CHECK(ui.error == UI_WIDGET_OK && commands.length == UI_ICON_COUNT-1);
    return commands;
}
static void widget_dispatch(void)
{
    CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),error,&errors) == UI_WIDGET_OK);
    Clay_RenderCommandArray commands = layout_icons();
    begin(1.25f);
    CHECK(ui_frame_clay(&frame,commands,NULL) == UI_RENDER_OK);
    CHECK(frame.quad_count == UI_ICON_COUNT-1 && frame.batch_count == 1);
    CHECK(frame.quads[0].color[1] == 128.0f/255);
    /* The tag decides ownership without dereferencing an unrelated payload. */
    Clay_RenderCommand other = {.commandType=CLAY_RENDER_COMMAND_TYPE_CUSTOM};
    other.renderData.custom.customData = (void *)(uintptr_t)1;
    UiClayBridge bridge = {.custom=custom};
    CHECK(ui_frame_clay(&frame,(Clay_RenderCommandArray){1,1,&other},&bridge) == UI_RENDER_OK);
    CHECK(custom_calls == 1);
    Clay_RenderCommand tagged = commands.internalArray[0];
    tagged.renderData.custom.customData = NULL;
    begin(1);
    CHECK(ui_frame_clay(&frame,(Clay_RenderCommandArray){1,1,&tagged},&bridge) == UI_RENDER_INVALID);
    CHECK(custom_calls == 1);
    tagged = commands.internalArray[0];
    unsigned char saved[4]; memcpy(saved,tagged.renderData.custom.customData,4);
    memset(tagged.renderData.custom.customData,0,4);
    begin(1);
    CHECK(ui_frame_clay(&frame,(Clay_RenderCommandArray){1,1,&tagged},NULL) == UI_RENDER_INVALID);
    memcpy(tagged.renderData.custom.customData,saved,4);
    ui_heap_begin();
    for (unsigned i = 0; i < 1000; ++i) {
        commands = layout_icons(); begin(1);
        CHECK(ui_frame_clay(&frame,commands,NULL) == UI_RENDER_OK);
    }
    ui_heap_end();
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    ui.scratch_bytes = 1;
    ui_icon(&ui,CLAY_ID("full"),UI_ICON_HOME,24,(Clay_Color){255,255,255,255},false);
    CHECK(ui.error == UI_WIDGET_FULL && errors == 1);
    (void)ui_widgets_end(&ui); ui.scratch_bytes = sizeof(scratch);
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    ui_icon(&ui,CLAY_ID("invalid"),UI_ICON_COUNT,24,(Clay_Color){255,255,255,255},false);
    CHECK(ui.error == UI_WIDGET_INVALID && errors == 2);
    (void)ui_widgets_end(&ui);
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    ui_icon(&ui,CLAY_ID("bad-size"),UI_ICON_HOME,NAN,(Clay_Color){255,255,255,255},false);
    CHECK(ui.error == UI_WIDGET_INVALID && errors == 3);
    (void)ui_widgets_end(&ui);
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    ui_icon(&ui,CLAY_ID("bad-color"),UI_ICON_HOME,24,(Clay_Color){256,255,255,255},false);
    CHECK(ui.error == UI_WIDGET_INVALID && errors == 4);
    (void)ui_widgets_end(&ui);
    CHECK(ui_widgets_begin(&ui,800,600,(UiWidgetInput){0}) == UI_WIDGET_OK);
    for (unsigned i = 0; i < 401; ++i) {
        ui_icon(&ui,CLAY_IDI("limit",i),UI_ICON_NONE,1,(Clay_Color){255,255,255,255},false);
    }
    CHECK(ui.error == UI_WIDGET_FULL && errors == 5);
    (void)ui_widgets_end(&ui);
    CHECK(ui_widgets_dispose(&ui) == UI_WIDGET_OK);
}
int main(void)
{
    geometry(); invalid_arguments(); widget_dispatch();
    puts("ui_icons: aspect fit, DPI invariance, RTL, shader validation, Clay dispatch, budgets and warm allocation checks passed");
    return 0;
}
