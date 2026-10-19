#include "widgets.h"
#include <math.h>

static Clay_ElementId child_id(Clay_ElementId parent, uint64_t key)
{
    /* Both halves of a persistent model key participate; viewport slots don't. */
    key ^= key >> 30; key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27; key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 31;
    uint32_t hash = parent.id ^ (uint32_t)key ^ (uint32_t)(key >> 32);
    return (Clay_ElementId){.id=hash ? hash : 1};
}
UiWidgetResult ui_virtual_list(UiWidgets *ui, Clay_ElementId id, UiVirtualList *list,
    uint32_t total, double height, double viewport, uint32_t overscan,
    UiListKey key_at, UiListRow row, void *model, UiListRange *out)
{
    if (!ui || !ui->in_frame || !list || !key_at || !row || !out || !id.id ||
        height > 1000000 || viewport > 1000000) { return UI_WIDGET_INVALID; }
    if (ui->error != UI_WIDGET_OK) { return ui->error; }
    UiWidgetResult result = ui_virtual_range(total,height,list->scroll,viewport,overscan,out);
    if (result != UI_WIDGET_OK) { ui_widgets_fail(ui,result); return result; }
    if (ui->declarations + out->end - out->first + 4 > 400) {
        ui_widgets_fail(ui,UI_WIDGET_FULL); return ui->error;
    }
    ui->declarations += out->end - out->first + 4;
    Clay_ElementId bar = child_id(id,UINT64_MAX);
    Clay_ElementData data = Clay_GetElementData(bar);
    bool bar_over = Clay_PointerOver(bar);
    if (bar_over && ui->input.pressed && !ui->active) { ui->active = bar.id; }
    if (ui->active == bar.id && data.found) {
        float thumb = fminf((float)viewport,fmaxf(16,(float)(viewport * out->thumb_size)));
        double position = data.boundingBox.height > thumb ?
            (ui->input.y - data.boundingBox.y - thumb / 2) / (data.boundingBox.height - thumb) : 0;
        (void)ui_virtual_scrollbar(list,out,position);
        (void)ui_virtual_range(total,height,list->scroll,viewport,overscan,out);
        if (ui->input.released || ui->input.cancel) { ui->active = 0; }
    }
    list->scroll = out->scroll; list->hover = 0;
    bool over = Clay_PointerOver(id);
    Clay_ElementData bounds = Clay_GetElementData(id); uint32_t hit;
    if (over && !bar_over && bounds.found && ui_virtual_hit(out,ui->input.y - bounds.boundingBox.y,&hit)) {
        list->hover = key_at(model,hit);
        if (ui->input.pressed) { list->focus = list->selection = list->hover; ui->focus = id.id; }
    }
    float offset = (float)(out->before - out->scroll);
    float rows_height = (float)((out->end - out->first) * height);
    CLAY(id,{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED((float)viewport)}}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED((float)viewport)}},
            .clip={.horizontal=true,.vertical=true,.childOffset={0,offset}}}) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(rows_height)},
                .layoutDirection=CLAY_TOP_TO_BOTTOM}}) {
                for (uint32_t i = out->first; i < out->end; ++i) {
                    uint64_t key = key_at(model,i);
                    if (!key) { ui_widgets_fail(ui,UI_WIDGET_INVALID); break; }
                    Clay_ElementId row_id = child_id(id,key);
                    Clay_Color color = list->selection == key ? (Clay_Color){35,70,50,255} :
                        list->hover == key ? (Clay_Color){50,50,50,255} : (Clay_Color){20,20,20,255};
                    CLAY(row_id,{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED((float)height)}},
                        .backgroundColor=color,.border={.color={180,180,180,255},
                        .width=CLAY_BORDER_ALL(list->focus == key ? 1 : 0)}}) { row(model,i,key); }
                }
            }
        }
        float thumb = fminf((float)viewport,fmaxf(16,(float)(viewport * out->thumb_size)));
        float top = (float)out->thumb_position * ((float)viewport - thumb);
        CLAY(bar,{.layout={.sizing={CLAY_SIZING_FIXED(8),CLAY_SIZING_FIXED((float)viewport)},
            .layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor={25,25,25,255}}) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(8),CLAY_SIZING_FIXED(top)}}}) { }
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(8),CLAY_SIZING_FIXED(thumb)}},
                .backgroundColor={110,110,110,255},.cornerRadius=CLAY_CORNER_RADIUS(4)}) { }
        }
    }
    return ui->error;
}
