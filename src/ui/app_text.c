#include "app.h"
#include "font_roles.h"
#include <string.h>

static bool same(FontConfig a, FontConfig b)
{
    return a.text_generation==b.text_generation && a.face==b.face && a.size==b.size && a.dpi==b.dpi &&
        a.weight==b.weight && a.letter_spacing==b.letter_spacing && a.line_height==b.line_height &&
        a.max_width==b.max_width && a.italic==b.italic && a.rtl==b.rtl && a.ellipsis==b.ellipsis && a.measure_only==b.measure_only;
}
static Clay_Dimensions measure(Clay_StringSlice text, Clay_TextElementConfig *style, void *context)
{
    App *a=context; FontMeasureContext *font=a->fonts;
    FontConfig key=font_clay_config(font,style->fontId,style->fontSize,style->letterSpacing,style->lineHeight);
    key.measure_only=true;
    if (a->measure_generation!=a->model.generation || a->measure_dpi!=key.dpi) {
        a->measure_generation=a->model.generation; a->measure_dpi=key.dpi; a->measure_count=a->measure_next=0;
    }
    for (uint32_t i=0;i<a->measure_count;++i) {
        AppMeasure *m=&a->measures[i];
        if (m->length==(uint32_t)text.length && same(m->config,key) &&
            (!m->length || !memcmp(m->text,text.chars,m->length))) { return m->dimensions; }
    }
    FontHandle handle; FontMetrics metrics;
    FontResult result=font_lookup(font->cache,text,&key,&handle);
    if (result==FONT_OK && font_metrics(font->cache,handle,&metrics)==FONT_OK) {
        if (text.length<0 || text.length>=192) {
            ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return (Clay_Dimensions){0};
        }
        /* Cache frequent word probes and timestamps. Longer complete labels
         * stay in Clay's cache; retaining their bytes here wastes the UI budget. */
        if ((size_t)text.length>=sizeof(a->measures[0].text)) {
            return (Clay_Dimensions){metrics.width,metrics.height};
        }
        /* Progress timestamps change throughout a route's lifetime. Scalar
         * measurements are unpinned cache entries, so replace them in place. */
        uint32_t slot=a->measure_count<APP_MEASURES ? a->measure_count++ : a->measure_next++%APP_MEASURES;
        AppMeasure *m=&a->measures[slot];
        m->config=key; m->length=(uint32_t)text.length;
        if (m->length) { memcpy(m->text,text.chars,m->length); }
        m->dimensions=(Clay_Dimensions){metrics.width,metrics.height}; return m->dimensions;
    }
    (void)font_request_text(font->cache,text,&key);
    return (Clay_Dimensions){text.length*(float)key.size*0.5f,(float)key.size};
}
void app_bind_fonts(App *a, FontMeasureContext *font)
{
    if (!a || !a->alive || !font) { return; }
    a->fonts=font; a->measure_count=a->measure_next=0; a->measure_generation=a->model.generation;
    Clay_SetCurrentContext(a->widgets.clay); font_bind_clay(font);
    /* Clay measures word fragments as well as complete draw strings. Keeping
     * their scalar advances here prevents word probes evicting visible glyph
     * runs and scheduling an endless warm-up/idle repaint cycle. */
    Clay_SetMeasureTextFunction(measure,a);
}
