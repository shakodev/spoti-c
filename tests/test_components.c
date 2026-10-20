#include "ui/components.h"
#include "ui/component_showcase.h"
#include "ui/menu.h"
#include "ui/font_roles.h"
#include "ui/artwork_quad.h"
#include "ui/renderer_clay.h"
#include "support/test_check.h"
#include "support/ui_heap_watch.h"
#include <math.h>
#include <string.h>

static UiWidgets ui;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static unsigned errors;
static UiRenderFrame frame;
static void error(void *context, UiWidgetResult result)
{
    CHECK(context==&errors && result!=UI_WIDGET_OK); ++errors;
}
static bool same(Clay_Color a, Clay_Color b)
{
    return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
}
static void tokens(void)
{
    const UiTheme *t=ui_theme(); CHECK(t==ui_theme());
    CHECK(t->panel.r==18 && t->hover.r==42 && t->accent.g==215 && t->subdued.b==179);
    CHECK(t->space[0]==2 && t->space[6]==24 && t->space[11]==128);
    CHECK(t->control[0]==32 && t->control[1]==48 && t->control[2]==56 && t->focus_width==2);
    CHECK(t->body==16 && t->small==14 && t->title==24 && t->large_title==32);
    UiComponentStyle normal=ui_component_style(UI_COMPONENT_PRIMARY,0);
    UiComponentStyle hover=ui_component_style(UI_COMPONENT_PRIMARY,UI_CONTROL_HOVER);
    UiComponentStyle pressed=ui_component_style(UI_COMPONENT_PRIMARY,UI_CONTROL_PRESSED);
    CHECK(same(normal.background,t->accent) && same(normal.foreground,t->on_accent));
    CHECK(!same(normal.background,hover.background) && !same(hover.background,pressed.background));
    for (int kind=0;kind<UI_COMPONENT_KIND_COUNT;++kind) {
        UiComponentStyle disabled=ui_component_style((UiComponentKind)kind,UI_CONTROL_DISABLED);
        UiComponentStyle inert=ui_component_style((UiComponentKind)kind,
            UI_CONTROL_DISABLED|UI_CONTROL_HOVER|UI_CONTROL_PRESSED|UI_CONTROL_FOCUS);
        CHECK(same(disabled.foreground,inert.foreground) && same(disabled.background,inert.background));
        CHECK(disabled.foreground.a<=77 && disabled.border==inert.border);
        CHECK(ui_component_style((UiComponentKind)kind,UI_CONTROL_FOCUS).border==2);
        CHECK(ui_component_style((UiComponentKind)kind,UI_CONTROL_SELECTED).selected);
    }
    CHECK(ui_theme_opacity(t->text,NAN).a==0 && ui_theme_opacity(t->text,2).a==255);
}
static void artwork(void)
{
    const UiTexture texture=257;
    CHECK(ui_frame_begin(&frame,400,400,1.25f)==UI_RENDER_OK);
    CHECK(ui_artwork_draw(&frame,texture,200,100,(UiRect){4,8,100,100},4)==UI_RENDER_OK);
    CHECK(frame.quad_count==1 && frame.quads[0].uv[0]==0.25f && frame.quads[0].uv[2]==0.75f);
    CHECK(frame.quads[0].uv[1]==0 && frame.quads[0].uv[3]==1);
    CHECK(frame.quads[0].box[0]==4 && frame.quads[0].box[2]==100 && frame.quads[0].radii[0]==4);
    CHECK(frame.batches[0].texture==texture);
    CHECK(ui_artwork_draw(&frame,texture,100,200,(UiRect){0,0,100,100},0)==UI_RENDER_OK);
    CHECK(frame.quads[1].uv[0]==0 && frame.quads[1].uv[2]==1 &&
        frame.quads[1].uv[1]==0.25f && frame.quads[1].uv[3]==0.75f);
    CHECK(ui_artwork_draw(&frame,texture,100,100,(UiRect){0,0,100,100},50)==UI_RENDER_OK);
    CHECK(frame.quads[2].uv[0]==0 && frame.quads[2].uv[1]==0 &&
        frame.quads[2].uv[2]==1 && frame.quads[2].uv[3]==1);
    CHECK(ui_artwork_draw(&frame,texture,0,100,(UiRect){0,0,100,100},4)==UI_RENDER_INVALID);
    CHECK(frame.quad_count==3);
    CHECK(ui_frame_begin(&frame,400,400,1)==UI_RENDER_OK);
    CHECK(ui_artwork_draw(&frame,texture,100,100,(UiRect){0,0,100,100},51)==UI_RENDER_INVALID);
    CHECK(ui_frame_begin(&frame,400,400,1)==UI_RENDER_OK);
    CHECK(ui_artwork_draw(&frame,0,100,100,(UiRect){0,0,100,100},4)==UI_RENDER_INVALID);
    CHECK(ui_frame_begin(&frame,400,400,1)==UI_RENDER_OK);
    CHECK(ui_widgets_begin(&ui,300,300,(UiWidgetInput){.x=-1,.y=-1})==UI_WIDGET_OK);
    CHECK(!ui_component_card(&ui,CLAY_ID("texture-card"),CLAY_STRING("Card"),CLAY_STRING("Crop"),
        (UiCardArtwork){.texture=texture,.source_width=200,.source_height=100},
        (UiControl){.width=156,.height=216,.state=UI_CONTROL_DISABLED}));
    Clay_RenderCommandArray commands=ui_widgets_end(&ui); unsigned images=0;
    for (int32_t i=0;i<commands.length;++i) {
        bool handled=false;
        CHECK(ui_artwork_dispatch(&frame,&commands.internalArray[i],&handled)==UI_RENDER_OK);
        if (handled) { ++images; }
    }
    CHECK(images==1 && frame.quad_count==1 && frame.quads[0].box[2]==132);
    CHECK(frame.quads[0].color[3]==0.3f && frame.quads[0].uv[0]==0.25f);
}
static bool button(UiWidgetInput input, uint32_t state)
{
    CHECK(ui_widgets_begin(&ui,300,200,input)==UI_WIDGET_OK);
    bool activated=ui_component_button(&ui,CLAY_ID("test-button"),CLAY_STRING("Play"),
        UI_ICON_PLAY,UI_COMPONENT_PRIMARY,(UiControl){.width=120,.height=48,.state=state});
    Clay_RenderCommandArray commands=ui_widgets_end(&ui);
    CHECK(commands.length>=2 && ui.error==UI_WIDGET_OK);
    return activated;
}
static void capture(void)
{
    CHECK(!button((UiWidgetInput){.x=10,.y=10},0));
    CHECK(!button((UiWidgetInput){.x=10,.y=10,.pressed=true,.down=true},0));
    CHECK(ui.active==CLAY_ID("test-button").id && ui.focus==ui.active);
    CHECK(!button((UiWidgetInput){.x=220,.y=100,.released=true},0));
    CHECK(!ui.active);
    CHECK(!button((UiWidgetInput){.x=10,.y=10,.pressed=true,.down=true},0));
    CHECK(button((UiWidgetInput){.x=10,.y=10,.released=true},0));
    CHECK(!button((UiWidgetInput){.x=10,.y=10,.released=true},0));
    CHECK(button((UiWidgetInput){.activate=true},0));
    CHECK(!button((UiWidgetInput){.activate=true},UI_CONTROL_DISABLED));
    CHECK(!button((UiWidgetInput){.x=10,.y=10,.pressed=true,.down=true},0));
    CHECK(!button((UiWidgetInput){.x=10,.y=10,.released=true,.cancel=true},0));
    CHECK(!ui.active);
    CHECK(!button((UiWidgetInput){.x=10,.y=10,.pressed=true,.down=true},0));
    CHECK(!button((UiWidgetInput){.x=10,.y=10,.released=true},UI_CONTROL_DISABLED));
    CHECK(!ui.active);
}
static void focus(void)
{
    UiFocusItem items[]={{CLAY_ID("a"),false},{CLAY_ID("b"),true},{CLAY_ID("c"),false}};
    ui.focus=0;
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_NEXT,false) && ui.focus==items[0].id.id);
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_NEXT,false) && ui.focus==items[2].id.id);
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_NEXT,false) && ui.focus==items[0].id.id);
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_PREVIOUS,false) && ui.focus==items[2].id.id);
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_HOME,false) && ui.focus==items[0].id.id);
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_LEFT,true) && ui.focus==items[2].id.id);
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_RIGHT,true) && ui.focus==items[0].id.id);
    CHECK(ui_focus_move(&ui,items,3,UI_FOCUS_END,true) && ui.focus==items[2].id.id);
    items[0].disabled=true; items[2].disabled=true; uint32_t before=ui.focus;
    CHECK(!ui_focus_move(&ui,items,3,UI_FOCUS_NEXT,false) && ui.focus==before);
    CHECK(!ui_focus_move(&ui,items,0,UI_FOCUS_HOME,false));
}
typedef struct ClipCheck { Clay_BoundingBox owners[6]; unsigned texts; } ClipCheck;
static UiRenderResult clipped_text(void *context, UiRenderFrame *f, const Clay_RenderCommand *c)
{
    ClipCheck *check=context;
    CHECK(c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT && check->texts<6);
    Clay_BoundingBox owner=check->owners[check->texts++];
    CHECK(f->clip_count>1);
    UiRect clip=f->clips[f->clip_count-1];
    CHECK(clip.x>=owner.x && clip.y>=owner.y &&
        clip.x+clip.w<=owner.x+owner.width && clip.y+clip.h<=owner.y+owner.height);
    /* Deliberately oversized ink: containment must come from the renderer's
     * active scissor, regardless of Clay's text box sizing or truncation. */
    QuadInstance q={.box={c->boundingBox.x,c->boundingBox.y,1000,14},.color={1,1,1,1}};
    uint32_t before=f->quad_count;
    UiRenderResult result=ui_frame_quad(f,&q,0);
    if (result==UI_RENDER_OK && f->quad_count>before) {
        const UiBatch *batch=&f->batches[f->batch_count-1];
        CHECK(batch->scissor[0]>=(int32_t)floorf(owner.x) &&
            batch->scissor[1]>=(int32_t)floorf(owner.y) &&
            batch->scissor[2]<=(int32_t)ceilf(owner.x+owner.width) &&
            batch->scissor[3]<=(int32_t)ceilf(owner.y+owner.height));
    }
    return result;
}
static void clipped_labels(void)
{
    static const char long_label[]="A deliberately long localized label that cannot fit in this control";
    Clay_String label={true,(int32_t)(sizeof(long_label)-1),long_label};
    CHECK(ui_widgets_begin(&ui,400,800,(UiWidgetInput){.x=-1,.y=-1})==UI_WIDGET_OK);
    CLAY(CLAY_ID("clip-stack"),{.layout={.layoutDirection=CLAY_TOP_TO_BOTTOM}}) {
        (void)ui_component_button(&ui,CLAY_ID("clip-button"),label,UI_ICON_PLAY,
            UI_COMPONENT_PRIMARY,(UiControl){.width=40,.height=20});
        (void)ui_component_card(&ui,CLAY_ID("clip-card"),label,label,
            (UiCardArtwork){.background={20,20,20,255},.foreground={100,100,100,255}},
            (UiControl){.width=100,.height=180});
        (void)ui_component_row(&ui,CLAY_ID("clip-row"),label,label,UI_ICON_LIBRARY,
            UI_COMPONENT_ROW,(UiControl){.width=80,.height=40});
        ui_component_status(&ui,CLAY_ID("clip-status"),label,UI_STATUS_INFORMATION,80,false);
    }
    Clay_RenderCommandArray commands=ui_widgets_end(&ui);
    CHECK(ui.error==UI_WIDGET_OK);
    unsigned depth=0,texts=0;
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) {
            CHECK(depth<8); ++depth;
        } else if (c->commandType==CLAY_RENDER_COMMAND_TYPE_SCISSOR_END) {
            CHECK(depth>0); --depth;
        } else if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
            CHECK(depth>0); ++texts;
        }
    }
    CHECK(depth==0 && texts==6);
    Clay_BoundingBox button_bounds=Clay_GetElementData(CLAY_ID("clip-button")).boundingBox;
    Clay_BoundingBox card_bounds=Clay_GetElementData(CLAY_ID("clip-card")).boundingBox;
    Clay_BoundingBox row_bounds=Clay_GetElementData(CLAY_ID("clip-row")).boundingBox;
    Clay_BoundingBox status_bounds=Clay_GetElementData(CLAY_ID("clip-status")).boundingBox;
    ClipCheck check={.owners={button_bounds,card_bounds,card_bounds,row_bounds,row_bounds,status_bounds}};
    CHECK(ui_frame_begin(&frame,400,800,1)==UI_RENDER_OK);
    UiClayBridge bridge={.custom=clipped_text,.context=&check};
    CHECK(ui_frame_clay(&frame,commands,&bridge)==UI_RENDER_OK && check.texts==6);
}
static void menu(void)
{
    UiMenu m={0}; UiMenuItem items[]={
        {.id=CLAY_ID("item0"),.label=CLAY_STRING("Disabled"),.icon=UI_ICON_PLUS,.disabled=true},
        {.id=CLAY_ID("item1"),.label=CLAY_STRING("Play"),.icon=UI_ICON_PLAY},
        {.id=CLAY_ID("item2"),.label=CLAY_STRING("Saved"),.icon=UI_ICON_CHECK,.selected=true}};
    uint32_t selected=99; ui.focus=CLAY_ID("opener").id; uint32_t opener=ui.focus;
    CHECK(!ui_menu_take_activation(&m,&selected));
    CHECK(ui_menu_open(&m,&ui,items,3,false)==UI_WIDGET_OK);
    CHECK(m.open && ui.focus==items[1].id.id);
    CHECK(ui_menu_key(&m,&ui,UI_MENU_UP) && ui.focus==items[2].id.id);
    CHECK(ui_menu_key(&m,&ui,UI_MENU_DOWN) && ui.focus==items[1].id.id);
    CHECK(ui_menu_key(&m,&ui,UI_MENU_END) && ui.focus==items[2].id.id);
    CHECK(ui_menu_key(&m,&ui,UI_MENU_HOME) && ui.focus==items[1].id.id);
    CHECK(ui_menu_key(&m,&ui,UI_MENU_ENTER) && !m.open && ui.focus==opener);
    CHECK(ui_menu_take_activation(&m,&selected) && selected==1);
    CHECK(!ui_menu_take_activation(&m,&selected));
    CHECK(ui_menu_open(&m,&ui,items,3,true)==UI_WIDGET_OK);
    CHECK(ui_menu_key(&m,&ui,UI_MENU_ESCAPE) && !m.open && ui.focus==opener);
    CHECK(!ui_menu_take_activation(&m,&selected));
    CHECK(ui_menu_open(&m,&ui,items,3,false)==UI_WIDGET_OK);
    CHECK(ui_menu_open(&m,&ui,items,UI_MENU_ITEMS+1,false)==UI_WIDGET_FULL);
    CHECK(m.open && m.count==3);
    Clay_ElementId saved=items[2].id; items[2].id=items[1].id;
    CHECK(ui_menu_open(&m,&ui,items,3,false)==UI_WIDGET_INVALID); items[2].id=saved;
    CHECK(ui_widgets_begin(&ui,400,300,(UiWidgetInput){.x=-1,.y=-1})==UI_WIDGET_OK);
    ui_menu_draw(&m,&ui,CLAY_ID("menu"),240);
    Clay_RenderCommandArray commands=ui_widgets_end(&ui); CHECK(commands.length>0 && ui.error==UI_WIDGET_OK);
    CHECK(ui_menu_key(&m,&ui,UI_MENU_SPACE) && ui.focus==opener);
    CHECK(ui_menu_take_activation(&m,&selected) && selected==1);
    items[1].disabled=true; items[2].disabled=true;
    CHECK(ui_menu_open(&m,&ui,items,3,false)==UI_WIDGET_OK);
    CHECK(!ui_menu_key(&m,&ui,UI_MENU_ENTER));
    CHECK(ui_menu_key(&m,&ui,UI_MENU_ESCAPE) && ui.focus==opener);
}
static void showcase(bool rtl)
{
    CHECK(ui_widgets_begin(&ui,UI_SHOWCASE_WIDTH,UI_SHOWCASE_HEIGHT,
        (UiWidgetInput){.x=-10,.y=-10})==UI_WIDGET_OK);
    ui_component_showcase(&ui,rtl);
    Clay_RenderCommandArray commands=ui_widgets_end(&ui);
    CHECK(ui.error==UI_WIDGET_OK && commands.length>100);
    unsigned texts=0, icons=0, bold=0, rtl_text=0, borders=0;
    for (int32_t i=0;i<commands.length;++i) {
        const Clay_RenderCommand *c=&commands.internalArray[i];
        CHECK(isfinite(c->boundingBox.x) && isfinite(c->boundingBox.y));
        CHECK(c->boundingBox.width>=0 && c->boundingBox.height>=0);
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_TEXT) {
            ++texts;
            uint16_t font=c->renderData.text.fontId;
            if ((font&~FONT_ROLE_RTL)==FONT_ROLE_BOLD) { ++bold; }
            if (font&FONT_ROLE_RTL) { ++rtl_text; }
        }
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_CUSTOM) { ++icons; }
        if (c->commandType==CLAY_RENDER_COMMAND_TYPE_BORDER && c->renderData.border.width.left==2) { ++borders; }
    }
    CHECK(texts>40 && bold>20 && icons>20 && borders>=7 && rtl_text>0);
    if (rtl) { CHECK(rtl_text==texts); }
    Clay_ElementData normal=Clay_GetElementData(CLAY_IDI("showcase-primary",0));
    Clay_ElementData focus=Clay_GetElementData(CLAY_IDI("showcase-primary",5));
    CHECK(normal.found && focus.found && normal.boundingBox.width==156 && normal.boundingBox.height==48);
    CHECK(rtl ? normal.boundingBox.x>focus.boundingBox.x : normal.boundingBox.x<focus.boundingBox.x);
}
int main(void)
{
    tokens();
    CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),error,&errors)==UI_WIDGET_OK);
    artwork(); capture(); focus(); clipped_labels(); menu(); ui.focus=ui.active=0;
    showcase(false); showcase(true);
    ui_heap_begin();
    for (unsigned i=0;i<256;++i) { showcase((i&1u)!=0); }
    ui_heap_end();
    CHECK(errors==0);
    CHECK(ui_widgets_dispose(&ui)==UI_WIDGET_OK);
    puts("components: tokens, capture, state matrix, RTL/focus, menus and bounded warm frames passed");
    return 0;
}
