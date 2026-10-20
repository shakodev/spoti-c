#include "ui/app.h"
#include "ui/app_browse.h"
#include "ui/components.h"
#include "ui/font_roles.h"
#include <math.h>

typedef struct LibraryLayout {
    App *app;
    uint32_t columns;
    float width, edge, height;
    bool grid;
} LibraryLayout;

static uint64_t key_at(void *context, uint32_t row)
{
    LibraryLayout *layout=context; App *a=layout->app;
    uint32_t index=a->browse.order[row*layout->columns];
    return (uint64_t)a->model.page->offset+index+1;
}
static void row(void *context, uint32_t index, uint64_t key)
{
    (void)key; LibraryLayout *layout=context; App *a=layout->app;
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(layout->height)},
        .childGap=12},.clip={.horizontal=true,.vertical=true}}) {
        uint32_t first=index*layout->columns;
        for (uint32_t column=0;column<layout->columns && first+column<a->browse.count;++column) {
            uint32_t item_index=a->browse.order[first+column];
            uint32_t item_key=a->model.page->offset+item_index+1;
            app_browse_card(a,CLAY_IDI("LibraryItem",item_key),&a->model.page->items[item_index],
                UINT32_MAX,item_index,layout->width,layout->edge,!layout->grid);
        }
    }
}
void app_library(App *a)
{
    app_browse_project(a);
    app_browse_query(a);
    bool grid=a->model.location.grid;
    uint32_t columns=grid ? app_browse_columns(a) : 1;
    float width=(fmaxf(1,a->geometry.main-32)-(float)(columns-1)*12)/(float)columns;
    LibraryLayout layout={.app=a,.columns=columns,.width=width,.grid=grid,
        .edge=grid ? fmaxf(1,width-16) : 48};
    layout.height=grid ? layout.edge+68 : 64;
    float viewport=fminf(fmaxf(0,a->geometry.viewport-90),grid ? 2*layout.height : 384);
    if (a->browse.count) {
        UiListRange range;
        uint32_t rows=(a->browse.count+columns-1)/columns;
        (void)ui_virtual_list(&a->widgets,CLAY_ID("AppList"),&a->widgets.lists[0],
            rows,layout.height,viewport,0,key_at,row,&layout,&range);
    } else {
        a->widgets.lists[0].scroll=0;
        CLAY(CLAY_ID("AppList"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(48)},
            .childAlignment={.y=CLAY_ALIGN_Y_CENTER}},.clip={.horizontal=true,.vertical=true}}) {
            ui_component_label(CLAY_STRING("No matches on this loaded page."),
                FONT_ROLE_BODY,ui_theme()->body,ui_theme()->subdued,false);
        }
    }
    if (a->control_count<APP_CONTROLS) {
        a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("AppList"),
            .action={.type=APP_SELECT,.value=-1},.disabled=!a->browse.count};
    } else { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); }
    /* Pagination remains reachable even when the current loaded page has no
     * matches. Filtering cannot claim to search unloaded collection items. */
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},.childGap=8}}) {
        (void)app_button(a,CLAY_ID("AppMore"),
            a->model.pending ? CLAY_STRING("Loading...") : CLAY_STRING("Load more"),112,
            (AppAction){.type=APP_MORE},a->model.pending || a->model.page->next == COLLECTION_END);
        if (a->model.page->partial) { app_message(a,CLAY_STRING("Some items unavailable"),APP_WARNING); }
    }
}
