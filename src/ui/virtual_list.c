#include "virtual_list.h"
#include <math.h>
#include <string.h>

UiWidgetResult ui_virtual_range(uint32_t total, double height, double scroll,
    double viewport, uint32_t overscan, UiListRange *out)
{
    if (!out) { return UI_WIDGET_INVALID; }
    memset(out,0,sizeof(*out));
    if (!isfinite(height) || height <= 0 || !isfinite(scroll) ||
        !isfinite(viewport) || viewport < 0) { return UI_WIDGET_INVALID; }
    double extent = total * height;
    if (!isfinite(extent)) { return UI_WIDGET_INVALID; }
    double maximum = fmax(0,extent - viewport);
    scroll = fmin(fmax(scroll,0),maximum);
    out->scroll = scroll; out->extent = extent;
    out->row_height = height; out->viewport = viewport;
    out->thumb_size = extent > 0 ? fmin(1,viewport / extent) : 1;
    out->thumb_position = maximum > 0 ? scroll / maximum : 0;
    if (!total || viewport == 0) { out->after = extent; return UI_WIDGET_OK; }
    /* Clamp in floating point before converting: no huge-offset integer casts. */
    uint32_t first = (uint32_t)fmin(total,floor(scroll / height));
    uint32_t end = (uint32_t)fmin(total,ceil((scroll + fmin(viewport,extent)) / height));
    out->visible_first = first; out->visible_end = end;
    if (overscan > UI_LIST_OVERSCAN) { overscan = UI_LIST_OVERSCAN; }
    out->first = first > overscan ? first - overscan : 0;
    out->end = end + (total - end < overscan ? total - end : overscan);
    out->before = out->first * height;
    out->after = (total - out->end) * height;
    return out->end - out->first > UI_LIST_ROWS ? UI_WIDGET_FULL : UI_WIDGET_OK;
}

UiWidgetResult ui_virtual_navigate(UiVirtualList *list, uint32_t total,
    double height, double viewport, UiNav nav, bool select,
    UiListKey key_at, UiListIndex index_of, void *model)
{
    UiListRange range;
    if (!list || !key_at || !index_of || nav > UI_NAV_PAGE_DOWN || nav < UI_NAV_UP) {
        return UI_WIDGET_INVALID;
    }
    UiWidgetResult result = ui_virtual_range(total,height,list->scroll,viewport,0,&range);
    if (result != UI_WIDGET_OK) { return result; }
    if (!total) { return UI_WIDGET_CANCEL; }
    uint32_t row = 0;
    bool found = list->focus && index_of(model,list->focus,&row) && row < total;
    uint32_t page = (uint32_t)fmin(total,fmax(1,floor(viewport / height)));
    if (nav == UI_NAV_END) { row = total - 1; }
    else if (nav == UI_NAV_HOME || !found) { row = 0; }
    else if (nav == UI_NAV_UP) { row -= row > 0; }
    else if (nav == UI_NAV_DOWN) { row += row < total - 1; }
    else if (nav == UI_NAV_PAGE_UP) { row = row > page ? row - page : 0; }
    else if (nav == UI_NAV_PAGE_DOWN) { row += total - 1 - row < page ? total - 1 - row : page; }
    uint64_t key = key_at(model,row);
    if (!key) { return UI_WIDGET_INVALID; }
    list->focus = key;
    if (select) { list->selection = key; }
    double top = row * height, bottom = top + height;
    list->scroll = range.scroll;
    if (top < list->scroll) { list->scroll = top; }
    else if (bottom > list->scroll + viewport) { list->scroll = fmax(0,bottom - viewport); }
    return UI_WIDGET_OK;
}

bool ui_virtual_hit(const UiListRange *range, double y, uint32_t *row)
{
    if (!range || !row || !isfinite(y) || y < 0 || y >= range->viewport ||
        range->row_height <= 0 || range->scroll + y >= range->extent) { return false; }
    *row = (uint32_t)floor((range->scroll + y) / range->row_height);
    return true;
}

UiWidgetResult ui_virtual_scrollbar(UiVirtualList *list, const UiListRange *range, double top)
{
    if (!list || !range || !isfinite(top)) { return UI_WIDGET_INVALID; }
    list->scroll = fmin(1,fmax(0,top)) * fmax(0,range->extent - range->viewport);
    return UI_WIDGET_OK;
}
