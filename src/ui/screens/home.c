#include "ui/app.h"
#include "ui/app_browse.h"
#include "ui/components.h"
#include "ui/font_roles.h"
#include <math.h>
#include <string.h>

typedef struct HomeLayout {
    App *app;
    uint32_t columns;
    float width, edge, height;
} HomeLayout;

static uint64_t key_at(void *context, uint32_t index)
{
    HomeLayout *layout=context;
    return (uint64_t)layout->app->model.page->offset+index+1;
}
static void shelf(void *context, uint32_t index, uint64_t key)
{
    (void)key;
    HomeLayout *layout=context; App *a=layout->app;
    const CollectionPage *page=a->model.page;
    AppEntity entity={0}; AppResult result=app_entity(&page->items[index],&entity);
    const UiTheme *theme=ui_theme();
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(layout->height)},
        .childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
        .clip={.horizontal=true,.vertical=true}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},
            .childGap=8,.childAlignment={.y=CLAY_ALIGN_Y_CENTER}}}) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)}},
                .clip={.horizontal=true,.vertical=true}}) {
                Clay_String title=result==APP_OK ?
                    (Clay_String){false,(int32_t)strlen(entity.title),entity.title} :
                    CLAY_STRING("Unavailable shelf");
                ui_component_label(app_text(a,title),FONT_ROLE_TITLE,theme->title,theme->text,false);
            }
            (void)app_button(a,CLAY_IDI("ShelfMore",index),CLAY_STRING("View"),52,
                (AppAction){.type=APP_SELECT,.entity=entity},
                result!=APP_OK || !page->items[index].available);
        }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(layout->edge+68)},
            .childGap=12}}) {
            for (uint32_t column=0;column<layout->columns;++column) {
                CollectionItem item;
                if (collection_shelf_item(page,index,column,&item)!=COLLECTION_OK) { break; }
                app_browse_card(a,CLAY_IDI("HomeCard",index*8+column),&item,
                    index,column,layout->width,layout->edge,false);
            }
        }
    }
}
void app_home(App *a)
{
    uint32_t columns=app_browse_columns(a);
    float width=(fmaxf(1,a->geometry.main-32)-(float)(columns-1)*12)/(float)columns;
    HomeLayout layout={.app=a,.columns=columns,.width=width,.edge=fmaxf(1,width-16)};
    layout.height=layout.edge+104;
    /* At most two complete shelves plus one partial shelf: 3 * 5 images.
     * These native density choices are recorded in shell-home-library.md. */
    UiListRange range;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("AppList"),&a->widgets.lists[0],
        a->model.page->count,layout.height,fminf(a->geometry.viewport,2*layout.height),
        0,key_at,shelf,&layout,&range);
    if (a->model.page->next!=COLLECTION_END) {
        (void)app_button(a,CLAY_ID("AppMore"),
            a->model.pending ? CLAY_STRING("Loading...") : CLAY_STRING("Load more"),112,
            (AppAction){.type=APP_MORE},a->model.pending);
    }
}
