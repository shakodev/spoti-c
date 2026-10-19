#include "ui/app.h"
#include <math.h>
#include <string.h>

static uint64_t key_at(void *context, uint32_t index)
{
    App *a = context; return (uint64_t)a->model.page->offset + index + 1;
}
static void row(void *context, uint32_t index, uint64_t key)
{
    App *a = context; const CollectionItem *item = &a->model.page->items[index];
    AppEntity entity = {0}; bool valid = app_entity(item,&entity) == APP_OK;
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(48)},.padding={8,8,8,8}},
        .clip={.horizontal=true,.vertical=true}}) {
        (void)app_button(a,CLAY_IDI("LibraryItem",(uint32_t)key),valid ?
            (Clay_String){false,(int32_t)strlen(entity.title),entity.title} : CLAY_STRING("Unavailable item"),
            a->geometry.main-56,(AppAction){.type=APP_SELECT,.entity=entity,.value=index},!valid || !item->available);
    }
}
void app_library(App *a)
{
    UiListRange range;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("AppList"),&a->widgets.lists[0],a->model.page->count,
        48,fminf(384,a->geometry.viewport),0,key_at,row,a,&range);
    if (a->control_count < APP_CONTROLS) {
        a->controls[a->control_count++] = (AppControl){.id=CLAY_ID("AppList"),.action={.type=APP_SELECT,.value=-1}};
    }
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},.childGap=8}}) {
        (void)app_button(a,CLAY_ID("AppMore"),a->model.pending ? CLAY_STRING("Loading...") : CLAY_STRING("Load more"),112,
            (AppAction){.type=APP_MORE},a->model.pending || a->model.page->next == COLLECTION_END);
        if (a->model.page->partial) { app_label(CLAY_STRING("Some items unavailable"),14,(Clay_Color){180,180,180,255}); }
    }
}
