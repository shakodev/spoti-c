#include "widgets.h"
#include <math.h>
#include <string.h>

void ui_widgets_fail(UiWidgets *ui, UiWidgetResult error)
{
    if (ui && error != UI_WIDGET_OK && ui->error == UI_WIDGET_OK) {
        ui->error = error; if (ui->report) { ui->report(ui->report_context,error); }
    }
}
static void clay_error(Clay_ErrorData data) { ui_widgets_fail(data.userData,UI_WIDGET_FULL); }
static Clay_Dimensions fallback_measure(Clay_StringSlice text, Clay_TextElementConfig *config, void *context)
{
    (void)context; return (Clay_Dimensions){text.length * (float)config->fontSize * 0.5f,(float)config->fontSize};
}
UiWidgetResult ui_widgets_init(UiWidgets *ui, void *memory, size_t bytes,
    void *scratch, size_t scratch_bytes, UiWidgetError report, void *context)
{
    if (!ui || !report) { return UI_WIDGET_INVALID; }
    memset(ui,0,sizeof(*ui)); ui->report = report; ui->report_context = context;
    if (!memory || (uintptr_t)memory % 64 || !scratch || bytes > UI_CLAY_BYTES ||
        scratch_bytes > UI_SCRATCH_BYTES || scratch_bytes == 0) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return ui->error;
    }
    /* Set on the context-free defaults, never on another live context. */
    Clay_Context *previous = Clay_GetCurrentContext(); Clay_SetCurrentContext(NULL);
    Clay_SetMaxElementCount(512); Clay_SetMaxMeasureTextCacheWordCount(1024);
    if (bytes < Clay_MinMemorySize()) {
        Clay_SetCurrentContext(previous); ui_widgets_fail(ui,UI_WIDGET_FULL); return ui->error;
    }
    ui->clay = Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(bytes,memory),
        (Clay_Dimensions){1,1},(Clay_ErrorHandler){clay_error,ui});
    ui->scratch = scratch; ui->scratch_bytes = scratch_bytes;
    Clay_SetMeasureTextFunction(fallback_measure,NULL);
    for (uint32_t i = 0; i < UI_WIDGET_TEXTS; ++i) { ui->texts[i].blink = UI_CARET_NEVER; }
    return ui->error;
}
UiWidgetResult ui_widgets_dispose(UiWidgets *ui)
{
    if (!ui || ui->in_frame) { return UI_WIDGET_INVALID; }
    if (Clay_GetCurrentContext() == ui->clay) { Clay_SetCurrentContext(NULL); }
    memset(ui,0,sizeof(*ui)); return UI_WIDGET_OK;
}
void *ui_widgets_scratch(UiWidgets *ui, size_t bytes, size_t alignment)
{
    if (!ui || !ui->in_frame || ui->error != UI_WIDGET_OK) { return NULL; }
    if (!alignment || (alignment & (alignment - 1))) { ui_widgets_fail(ui,UI_WIDGET_INVALID); return NULL; }
    uintptr_t address = (uintptr_t)ui->scratch + ui->scratch_used;
    size_t padding = (size_t)((~address + 1) & (alignment - 1));
    if (padding > ui->scratch_bytes - ui->scratch_used ||
        bytes > ui->scratch_bytes - ui->scratch_used - padding) {
        ui_widgets_fail(ui,UI_WIDGET_FULL); return NULL;
    }
    ui->scratch_used += padding + bytes; return (void *)(address + padding);
}
UiWidgetResult ui_widgets_begin(UiWidgets *ui, float width, float height, UiWidgetInput input)
{
    if (!ui || !ui->clay || ui->in_frame || !isfinite(width) || !isfinite(height) ||
        width <= 0 || height <= 0 || !isfinite(input.x) || !isfinite(input.y)) { return UI_WIDGET_INVALID; }
    ui->error = UI_WIDGET_OK; ui->scratch_used = 0; ui->declarations = 0;
    ui->in_frame = true; ui->input = input; ui->hover = 0;
    Clay_SetCurrentContext(ui->clay); Clay_SetLayoutDimensions((Clay_Dimensions){width,height});
    Clay_SetPointerState((Clay_Vector2){input.x,input.y},input.down);
    Clay_BeginLayout(); return ui->error;
}
Clay_RenderCommandArray ui_widgets_end(UiWidgets *ui)
{
    if (!ui || !ui->in_frame) { return (Clay_RenderCommandArray){0}; }
    Clay_RenderCommandArray commands = Clay_EndLayout(0);
    if (ui->error != UI_WIDGET_OK) {
        /* Discard partial commands; red panel and text survive caller failure. */
        Clay_BeginLayout();
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(320),CLAY_SIZING_FIXED(40)}},
            .backgroundColor={160,20,20,255}}) {
            CLAY_TEXT(CLAY_STRING("UI capacity exceeded"),CLAY_TEXT_CONFIG({.fontSize=16,.textColor={255,255,255,255}}));
        }
        commands = Clay_EndLayout(0);
    }
    if (ui->input.released || ui->input.cancel) {
        /* A captured control can disappear with a screen change or culling.
         * Materialized sliders already committed/canceled; retire the rest. */
        for (uint32_t i = 0; i < UI_WIDGET_SEEKS; ++i) {
            if (ui->seeks[i].dragging) { (void)ui_seek_cancel(&ui->seeks[i]); }
        }
        ui->active = 0;
    }
    ui->scratch_used = 0; ui->in_frame = false; return commands;
}
void ui_widgets_focus(UiWidgets *ui, Clay_ElementId id)
{
    if (ui) { ui->focus = id.id; }
}
static bool declare(UiWidgets *ui, Clay_ElementId id)
{
    if (!ui || !ui->in_frame || ui->error != UI_WIDGET_OK) { return false; }
    if (!id.id) { ui_widgets_fail(ui,UI_WIDGET_INVALID); return false; }
    if (++ui->declarations > 400) { ui_widgets_fail(ui,UI_WIDGET_FULL); return false; }
    return true;
}
static bool hovered(UiWidgets *ui, Clay_ElementId id)
{
    bool over = Clay_PointerOver(id); if (over) { ui->hover = id.id; } return over;
}
static bool button_event(UiWidgets *ui, Clay_ElementId id, bool disabled)
{
    bool over = hovered(ui,id), activated = false;
    if (disabled) { if (ui->active == id.id) { ui->active = 0; } return false; }
    if (over && ui->input.pressed && !ui->active) { ui->active = ui->focus = id.id; }
    if (ui->input.released && ui->active == id.id) { activated = over && !ui->input.cancel; ui->active = 0; }
    return activated || (ui->focus == id.id && ui->input.activate && !ui->input.cancel);
}
bool ui_button(UiWidgets *ui, Clay_ElementId id, Clay_String label, float width, float height, bool disabled)
{
    if (!declare(ui,id)) { return false; }
    if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return false;
    }
    bool activated = button_event(ui,id,disabled);
    Clay_Color color = disabled ? (Clay_Color){45,45,45,255} :
        ui->active == id.id ? (Clay_Color){35,110,60,255} :
        ui->hover == id.id ? (Clay_Color){70,70,70,255} : (Clay_Color){50,50,50,255};
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(height)},
        .childAlignment={CLAY_ALIGN_X_CENTER,CLAY_ALIGN_Y_CENTER}},.backgroundColor=color,
        .cornerRadius=CLAY_CORNER_RADIUS(4),.border={.color={220,220,220,255},
        .width=CLAY_BORDER_ALL(ui->focus == id.id ? 1 : 0)}}) {
        CLAY_TEXT(label,CLAY_TEXT_CONFIG({.fontSize=16,.textColor={240,240,240,255}}));
    }
    return activated;
}
void ui_seek_slider(UiWidgets *ui, Clay_ElementId id, UiSeekSlider *slider, float width)
{
    if (!declare(ui,id)) { return; }
    if (!slider || !isfinite(slider->value) || !isfinite(width) || width <= 0) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return;
    }
    Clay_ElementData data = Clay_GetElementData(id); bool over = hovered(ui,id);
    double value = data.found && data.boundingBox.width > 0 ?
        (ui->input.x - data.boundingBox.x) / data.boundingBox.width : slider->value;
    if (over && ui->input.pressed && !ui->active) {
        ui->active = ui->focus = id.id; (void)ui_seek_begin(slider,value);
    }
    if (ui->active == id.id) {
        if (ui->input.cancel) { (void)ui_seek_cancel(slider); ui->active = 0; }
        else {
            (void)ui_seek_move(slider,value);
            if (ui->input.released) { (void)ui_seek_commit(slider); ui->active = 0; }
        }
    }
    float fill = (float)fmin(1,fmax(0,slider->value)) * width;
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(16)}},
        .backgroundColor={60,60,60,255},.cornerRadius=CLAY_CORNER_RADIUS(8)}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(fill),CLAY_SIZING_FIXED(16)}},
            .backgroundColor={30,200,95,255},.cornerRadius=CLAY_CORNER_RADIUS(8)}) { }
    }
}
static void span(const char *s, uint32_t n, bool selected)
{
    if (!n) { return; }
    CLAY_AUTO_ID({.backgroundColor=selected ? (Clay_Color){35,90,150,255} : (Clay_Color){0}}) {
        CLAY_TEXT(((Clay_String){false,(int32_t)n,s}),CLAY_TEXT_CONFIG({.fontSize=16,
            .textColor={240,240,240,255},.wrapMode=CLAY_TEXT_WRAP_NONE}));
    }
}
static void caret(void)
{
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(1),CLAY_SIZING_FIXED(18)}},.backgroundColor={255,255,255,255}}) { }
}
void ui_text_input(UiWidgets *ui, Clay_ElementId id, UiTextInput *text, float width)
{
    if (!declare(ui,id)) { return; }
    if (!text || text->length >= UI_TEXT_BYTES || text->caret > text->length || text->anchor > text->length ||
        text->composition_length >= UI_COMPOSITION_BYTES || text->composition_caret > text->composition_length ||
        !isfinite(width) || width <= 0) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return;
    }
    if (hovered(ui,id) && ui->input.pressed) { ui->focus = id.id; }
    if (text->focused != (ui->focus == id.id)) { ui_text_focus(text,ui->focus == id.id,0); }
    uint32_t a = text->anchor < text->caret ? text->anchor : text->caret;
    uint32_t b = text->anchor > text->caret ? text->anchor : text->caret;
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(28)},
        .childAlignment={.y=CLAY_ALIGN_Y_CENTER}},.backgroundColor={30,30,30,255},
        .clip={.horizontal=true,.vertical=true}}) {
        span(text->text,a,false);
        if (text->composing) {
            span(text->composition,text->composition_caret,true); caret();
            span(text->composition + text->composition_caret,text->composition_length - text->composition_caret,true);
        } else {
            if (text->focused && text->caret_visible && text->caret == a) { caret(); }
            span(text->text + a,b - a,true);
            if (text->focused && text->caret_visible && text->caret == b && a != b) { caret(); }
        }
        span(text->text + b,text->length - b,false);
    }
}
