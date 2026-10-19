#include "widgets.h"
#include <math.h>

static double clamp(double value) { return fmin(1,fmax(0,value)); }
UiWidgetResult ui_seek_begin(UiSeekSlider *s, double value)
{
    if (!s || !isfinite(value) || !isfinite(s->value) || s->dragging) { return UI_WIDGET_INVALID; }
    s->origin = clamp(s->value); s->dragging = true;
    return ui_seek_move(s,value);
}
UiWidgetResult ui_seek_move(UiSeekSlider *s, double value)
{
    if (!s || !s->dragging || !isfinite(value)) { return UI_WIDGET_INVALID; }
    s->value = s->preview = clamp(value); s->has_preview = true; return UI_WIDGET_OK;
}
UiWidgetResult ui_seek_commit(UiSeekSlider *s)
{
    if (!s || !s->dragging) { return UI_WIDGET_INVALID; }
    s->committed = s->value; s->has_commit = true; s->dragging = false;
    s->has_preview = false; return UI_WIDGET_OK;
}
UiWidgetResult ui_seek_cancel(UiSeekSlider *s)
{
    if (!s || !s->dragging) { return UI_WIDGET_INVALID; }
    s->value = s->origin; s->dragging = false; s->has_preview = false; return UI_WIDGET_CANCEL;
}
UiWidgetResult ui_seek_key(UiSeekSlider *s, double value)
{
    if (!s || s->dragging || !isfinite(value)) { return UI_WIDGET_INVALID; }
    s->value = s->committed = clamp(value); s->has_commit = true; s->has_preview = false;
    return UI_WIDGET_OK;
}
bool ui_seek_take_preview(UiSeekSlider *s, double *value)
{
    if (!s || !value || !s->has_preview) { return false; }
    *value = s->preview; s->has_preview = false; return true;
}
bool ui_seek_take_commit(UiSeekSlider *s, double *value)
{
    if (!s || !value || !s->has_commit) { return false; }
    *value = s->committed; s->has_commit = false; return true;
}
