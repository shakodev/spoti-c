#include "ui/components.h"
#include "ui/font.h"
#include "support/test_check.h"
#include "support/ui_heap_watch.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int ui_components_visual(void);
static UiWidgets ui;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static unsigned errors;
static UiRenderFrame frame;
static void failed(void *context, UiWidgetResult result) { (void)context; CHECK(result!=UI_WIDGET_OK); ++errors; }
static bool button(UiWidgetInput input, bool disabled, bool selected)
{
    CHECK(ui_widgets_begin(&ui,400,320,input)==UI_WIDGET_OK);
    bool hit=ui_component_button(&ui,CLAY_ID("Tab"),CLAY_STRING("Albums"),90,32,disabled,
        (UiButtonStyle){.kind=UI_BUTTON_TAB,.selected=selected});
    (void)ui_widgets_end(&ui); CHECK(ui.error==UI_WIDGET_OK); return hit;
}
static void input_states(void)
{
    CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),failed,NULL)==UI_WIDGET_OK);
    CHECK(!button((UiWidgetInput){.x=-1,.y=-1},false,true));
    CHECK(!button((UiWidgetInput){.x=8,.y=8,.down=true,.pressed=true},false,true));
    CHECK(ui.active==CLAY_ID("Tab").id && ui.focus==CLAY_ID("Tab").id);
    CHECK(!button((UiWidgetInput){.x=200,.y=8,.released=true},false,true)); /* release outside */
    CHECK(!ui.active);
    CHECK(!button((UiWidgetInput){.x=8,.y=8,.down=true,.pressed=true},false,false));
    CHECK(!button((UiWidgetInput){.x=8,.y=8,.released=true,.cancel=true},false,false));
    CHECK(button((UiWidgetInput){.activate=true},false,true)); /* selected remains operable */
    CHECK(!button((UiWidgetInput){.activate=true},true,true));
    CHECK(!button((UiWidgetInput){.x=8,.y=8,.down=true,.pressed=true},true,false));
    CHECK(!ui.active);
    UiComponentColors selected=ui_component_colors((UiButtonStyle){.kind=UI_BUTTON_TAB,.selected=true},false,false,false,false);
    UiComponentColors disabled=ui_component_colors((UiButtonStyle){.kind=UI_BUTTON_TAB,.selected=true},true,true,true,true);
    CHECK(selected.background.r==255 && selected.text.r==0);
    CHECK(disabled.text.r==124 && disabled.border_width==0);
    UiComponentColors focus=ui_component_colors((UiButtonStyle){.kind=UI_BUTTON_PRIMARY},false,false,false,true);
    CHECK(focus.border_width==2 && focus.background.g==215);
    ui_heap_begin(); for (unsigned n=0;n<2000;++n) { CHECK(!button((UiWidgetInput){0},false,n%2!=0)); } ui_heap_end();
    CHECK(!errors && ui_widgets_dispose(&ui)==UI_WIDGET_OK);
}
static void glyph_geometry(void)
{
    FontMeasureContext measure={.config={.dpi=144,.weight=400,.max_width=260,.ellipsis=true},.heading_width=668};
    FontConfig heading=font_clay_config(&measure,FONT_FACE_HEADING,48,0,0);
    CHECK(heading.face==0 && heading.weight==700 && heading.max_width==668 && heading.ellipsis);
    CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),failed,NULL)==UI_WIDGET_OK);
    const float scales[]={1,1.25f,1.5f,2};
    for (unsigned pass=0;pass<4;++pass) {
        CHECK(ui_widgets_begin(&ui,400,320,(UiWidgetInput){0})==UI_WIDGET_OK);
        CLAY_AUTO_ID({.layout={.layoutDirection=CLAY_TOP_TO_BOTTOM}}) {
            for (UiIcon icon=UI_ICON_PLAY;icon<UI_ICON_COUNT;++icon) { ui_icon(&ui,icon,24,UI_TEXT); }
        }
        Clay_RenderCommandArray commands=ui_widgets_end(&ui);
        CHECK(ui_frame_begin(&frame,(uint32_t)(400*scales[pass]),(uint32_t)(320*scales[pass]),scales[pass])==UI_RENDER_OK);
        unsigned glyphs=0;
        for (int32_t i=0;i<commands.length;++i) {
            const Clay_RenderCommand *c=&commands.internalArray[i];
            if (ui_icon_command(c)) {
                CHECK(c->boundingBox.width==24 && c->boundingBox.height==24);
                CHECK(ui_icon_render(&frame,c)==UI_RENDER_OK); ++glyphs;
            }
        }
        CHECK(glyphs==UI_ICON_COUNT-1 && frame.quad_count==glyphs);
        for (uint32_t n=0;n<frame.quad_count;++n) { CHECK(frame.quads[n].effect[0]==UI_QUAD_ICON && !frame.batches[0].texture); }
    }
    CHECK(ui_widgets_begin(&ui,400,320,(UiWidgetInput){0})==UI_WIDGET_OK);
    CHECK(!ui_component_button(&ui,CLAY_ID("Invalid"),CLAY_STRING(""),NAN,32,false,(UiButtonStyle){0}));
    (void)ui_widgets_end(&ui); CHECK(ui.error==UI_WIDGET_INVALID && errors==1);
    CHECK(ui_widgets_dispose(&ui)==UI_WIDGET_OK);
}
int main(int argc, char **argv)
{
    input_states(); glyph_geometry();
    puts("ui_components: selected/disabled/focus/capture, scalable glyph bounds and 2000 allocation-free frames passed");
    return argc==2 && !strcmp(argv[1],"--visual") ? ui_components_visual() : 0;
}
