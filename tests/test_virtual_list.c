#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "ui/widgets.h"
#include "support/test_check.h"
#include "support/ui_heap_watch.h"
#include <float.h>
#include <math.h>
#include <string.h>

static UiWidgets ui;
static _Alignas(64) unsigned char clay[UI_CLAY_BYTES];
static unsigned char scratch[UI_SCRATCH_BYTES];
static uint32_t calls, key_calls, indices, first_row;
static uint32_t row_twenty_id;
static bool reverse_order;
static uint32_t model_total = 10000;
static void error(void *context, UiWidgetResult result) { (void)context; (void)result; }
static uint64_t key_at(void *model, uint32_t index)
{
    (void)model; CHECK(index < model_total); ++key_calls;
    return UINT64_C(0x100000000) + (reverse_order ? model_total - index : index + 1);
}
static bool index_of(void *model, uint64_t key, uint32_t *index)
{
    (void)model; ++indices;
    if (key <= UINT64_C(0x100000000) || key > UINT64_C(0x100000000) + model_total) { return false; }
    uint32_t k = (uint32_t)(key - UINT64_C(0x100000000));
    *index = reverse_order ? model_total - k : k - 1; return true;
}
static void row(void *model, uint32_t index, uint64_t key)
{
    (void)model;
    if (!calls) { first_row = index; }
    CHECK(key > UINT64_C(0x100000000)); ++calls;
    if (index == 20) { row_twenty_id = Clay_GetOpenElementId(); }
    CLAY_TEXT(CLAY_STRING("A row"),CLAY_TEXT_CONFIG({.fontSize=16,.textColor={230,230,230,255}}));
}
static void boundaries(void)
{
    UiListRange r;
    CHECK(ui_virtual_range(0,20,DBL_MAX,400,UINT32_MAX,&r) == UI_WIDGET_OK);
    CHECK(r.first == 0 && r.end == 0 && r.scroll == 0 && r.thumb_size == 1);
    CHECK(ui_virtual_range(1,20,-DBL_MAX,400,UINT32_MAX,&r) == UI_WIDGET_OK && r.first == 0 && r.end == 1);
    CHECK(ui_virtual_range(10000,20,DBL_MAX,400,UINT32_MAX,&r) == UI_WIDGET_OK);
    CHECK(r.scroll == 199600 && r.end == 10000 && r.end - r.first == 36 && r.after == 0);
    CHECK(r.thumb_position == 1 && fabs(r.thumb_size - 0.002) < 1e-12);
    CHECK(ui_virtual_range(UINT32_MAX,19.2,DBL_MAX,400,16,&r) == UI_WIDGET_OK && r.end == UINT32_MAX);
    CHECK(ui_virtual_range(10000,20,0,0,4,&r) == UI_WIDGET_OK && r.end == 0);
    CHECK(ui_virtual_range(10000,20,NAN,400,4,&r) == UI_WIDGET_INVALID);
    CHECK(ui_virtual_range(10000,0,0,400,4,&r) == UI_WIDGET_INVALID);
    CHECK(ui_virtual_range(10000,DBL_MAX,0,400,4,&r) == UI_WIDGET_INVALID);
    CHECK(ui_virtual_range(10000,20,0,-1,4,&r) == UI_WIDGET_INVALID);
    CHECK(ui_virtual_range(10000,1,0,1000,16,&r) == UI_WIDGET_FULL);
    CHECK(ui_virtual_range(10000,20,19.5,20,0,&r) == UI_WIDGET_OK && r.first == 0 && r.end == 2);
    uint32_t hit;
    CHECK(ui_virtual_hit(&r,0.5,&hit) && hit == 1);
    CHECK(!ui_virtual_hit(&r,-0.1,&hit) && !ui_virtual_hit(&r,20,&hit));
    UiVirtualList list = {0};
    CHECK(ui_virtual_scrollbar(&list,&r,9) == UI_WIDGET_OK && list.scroll == r.extent - r.viewport);
    CHECK(ui_virtual_scrollbar(&list,&r,-9) == UI_WIDGET_OK && list.scroll == 0);
    CHECK(ui_virtual_scrollbar(&list,&r,NAN) == UI_WIDGET_INVALID);
}
static void properties(void)
{
    uint32_t seed = 23;
    for (unsigned i = 0; i < 50000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        uint32_t total = seed % 1000000;
        double height = 19.2 + (seed % 100) / 10.0;
        double viewport = (seed % 600) + 0.25;
        double scroll = ((double)(int32_t)seed / INT32_MAX) * total * height * 2;
        UiListRange r;
        CHECK(ui_virtual_range(total,height,scroll,viewport,seed % 100,&r) == UI_WIDGET_OK);
        CHECK(r.first <= r.visible_first && r.visible_first <= r.visible_end && r.visible_end <= r.end && r.end <= total);
        CHECK(r.visible_first - r.first <= UI_LIST_OVERSCAN && r.end - r.visible_end <= UI_LIST_OVERSCAN);
        CHECK(r.end - r.first <= (uint32_t)ceil(viewport / height) + 1 + 2 * UI_LIST_OVERSCAN);
        CHECK(r.scroll >= 0 && r.scroll <= fmax(0,r.extent - viewport));
        CHECK(fabs(r.before + (r.end - r.first) * height + r.after - r.extent) <= 1e-7);
        CHECK(r.thumb_size >= 0 && r.thumb_size <= 1 && r.thumb_position >= 0 && r.thumb_position <= 1);
        if (total && viewport > 0) {
            CHECK(r.visible_first * height <= r.scroll + 1e-7);
            CHECK(r.visible_end * height + 1e-7 >= fmin(r.extent,r.scroll + viewport));
        }
    }
}
static void navigation(void)
{
    UiVirtualList list = {0}; key_calls = indices = 0;
    CHECK(ui_virtual_navigate(&list,0,20,400,UI_NAV_DOWN,true,key_at,index_of,NULL) == UI_WIDGET_CANCEL);
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_DOWN,true,key_at,index_of,NULL) == UI_WIDGET_OK);
    CHECK(list.focus == key_at(NULL,0) && list.selection == list.focus);
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_END,false,key_at,index_of,NULL) == UI_WIDGET_OK);
    CHECK(list.focus == key_at(NULL,9999) && list.selection == key_at(NULL,0) && list.scroll == 199600);
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_UP,false,key_at,index_of,NULL) == UI_WIDGET_OK);
    CHECK(list.focus == key_at(NULL,9998));
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_PAGE_UP,true,key_at,index_of,NULL) == UI_WIDGET_OK);
    CHECK(list.focus == key_at(NULL,9978) && list.selection == list.focus);
    uint64_t selected = list.selection;
    reverse_order = true; /* Same key moves; navigation resolves the model key. */
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_DOWN,false,key_at,index_of,NULL) == UI_WIDGET_OK);
    CHECK(list.focus == key_at(NULL,22) && list.selection == selected);
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_HOME,true,key_at,index_of,NULL) == UI_WIDGET_OK);
    CHECK(list.scroll == 0 && list.focus == key_at(NULL,0));
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_UP,true,key_at,index_of,NULL) == UI_WIDGET_OK && list.scroll == 0);
    CHECK(ui_virtual_navigate(&list,10000,20,400,UI_NAV_PAGE_DOWN,true,key_at,index_of,NULL) == UI_WIDGET_OK);
    CHECK(list.focus == key_at(NULL,20)); reverse_order = false;
    CHECK(indices <= 8 && key_calls < 30);
}
static Clay_RenderCommandArray layout_frame(uint32_t total, double scroll, UiWidgetInput input, UiListRange *r)
{
    model_total = total; calls = key_calls = 0; ui.lists[0].scroll = scroll;
    CHECK(ui_widgets_begin(&ui,800,600,input) == UI_WIDGET_OK);
    CHECK(ui_virtual_list(&ui,CLAY_ID("list"),&ui.lists[0],total,19.2,384,4,key_at,row,NULL,r) == UI_WIDGET_OK);
    CHECK(calls == r->end - r->first && calls <= 29);
    CHECK(key_calls <= calls + 1);
    Clay_RenderCommandArray commands = ui_widgets_end(&ui); CHECK(ui.error == UI_WIDGET_OK);
    return commands;
}
static uint32_t find_row_id(Clay_RenderCommandArray commands, uint32_t first)
{
    for (int32_t i = 0; i < commands.length; ++i) {
        Clay_RenderCommand c = commands.internalArray[i];
        if (c.commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE && c.boundingBox.width > 100 &&
            fabsf(c.boundingBox.height - 19.2f) < 0.01f && first == first_row) { return c.id; }
    }
    return 0;
}
static void layout_checks(void)
{
    CHECK(ui_widgets_init(&ui,clay,sizeof(clay),scratch,sizeof(scratch),error,NULL) == UI_WIDGET_OK);
    UiListRange r; Clay_RenderCommandArray commands = layout_frame(10000,0,(UiWidgetInput){0},&r);
    uint32_t id = find_row_id(commands,0); CHECK(id != 0);
    ui.lists[0].focus = key_at(NULL,0); ui.lists[0].selection = key_at(NULL,0);
    uint64_t selected = ui.lists[0].selection;
    commands = layout_frame(10000,190000,(UiWidgetInput){0},&r);
    CHECK(ui.lists[0].focus == selected && ui.lists[0].selection == selected);
    commands = layout_frame(10000,0,(UiWidgetInput){0},&r);
    CHECK(find_row_id(commands,0) == id);
    (void)layout_frame(10000,19.2 * 10,(UiWidgetInput){0},&r);
    CHECK(row_twenty_id != 0); uint32_t stable = row_twenty_id; row_twenty_id = 0;
    (void)layout_frame(10000,19.2 * 20,(UiWidgetInput){0},&r);
    CHECK(row_twenty_id == stable); /* Same key, different materialized slot. */
    commands = layout_frame(10000,0,(UiWidgetInput){0},&r);
    unsigned clips = 0;
    for (int32_t i = 0; i < commands.length; ++i) {
        if (commands.internalArray[i].commandType == CLAY_RENDER_COMMAND_TYPE_SCISSOR_START) { ++clips; }
    }
    CHECK(clips > 0);
    (void)layout_frame(10000,0,(UiWidgetInput){.x=5,.y=25,.pressed=true,.down=true},&r);
    CHECK(ui.lists[0].hover == key_at(NULL,1) && ui.lists[0].selection == key_at(NULL,1));
    ui_heap_begin();
    for (unsigned i = 0; i < 3000; ++i) {
        (void)layout_frame(i % 2 ? 10000 : 1000000000,(double)(i % 1000) * 19.2,
            (UiWidgetInput){.x=5,.y=(float)(i % 400)},&r);
    }
    ui_heap_end();
    puts("3000 scroll/hover frames: Debug allocation hooks passed; <=29 materialized rows for 10k/1 billion models");
}
static void benchmark(void)
{
    LARGE_INTEGER frequency, start, end; CHECK(QueryPerformanceFrequency(&frequency));
    UiListRange r; const uint32_t totals[] = {10000,1000000000};
    for (unsigned k = 0; k < 2; ++k) {
        uint64_t rows = 0; CHECK(QueryPerformanceCounter(&start));
        for (unsigned i = 0; i < 10000; ++i) {
            (void)layout_frame(totals[k],(5000 + i % 1000) * 19.2,(UiWidgetInput){0},&r); rows += calls;
        }
        CHECK(QueryPerformanceCounter(&end));
        printf("total=%u: %.3f us/layout, %llu rows/10000 layouts\n",totals[k],
            (double)(end.QuadPart - start.QuadPart) * 1000000 / (double)frequency.QuadPart / 10000,
            (unsigned long long)rows);
    }
}
int main(int argc, char **argv)
{
    boundaries(); properties(); navigation(); layout_checks();
    if (argc == 2 && !strcmp(argv[1],"--bench")) { benchmark(); }
    CHECK(ui_widgets_dispose(&ui) == UI_WIDGET_OK && Clay_GetCurrentContext() == NULL);
    puts("ui_virtual_list: boundary/property checks, navigation, key stability, clipping and bounded layouts passed");
    return 0;
}
