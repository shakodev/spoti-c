#include "ui/app.h"
#include <math.h>
#include <string.h>

static uint64_t key_at(void *context, uint32_t index)
{
    App *a = context; return (uint64_t)a->model.page->offset + index + 1;
}
static void cover(App *a, const AppEntity *entity, float edge)
{
    AppImage *image = NULL;
    for (uint32_t i = 0; i < a->image_count; ++i) {
        if (!strcmp(a->images[i].entity.uri,entity->uri)) { image = &a->images[i]; break; }
    }
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(edge),CLAY_SIZING_FIXED(edge)}},
        .backgroundColor={40,48,44,255},.cornerRadius=CLAY_CORNER_RADIUS(4),.custom={.customData=image}}) { }
}
static void shelf(void *context, uint32_t index, uint64_t key)
{
    (void)key; App *a = context; const CollectionPage *page = a->model.page;
    AppEntity entity; AppResult result = app_entity(&page->items[index],&entity);
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(112)},
        .childGap=6,.layoutDirection=CLAY_TOP_TO_BOTTOM},.clip={.horizontal=true,.vertical=true}}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},.childGap=8}}) {
            CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)}},.clip={.horizontal=true}}) {
                app_label(app_text(a,result == APP_OK ? (Clay_String){false,(int32_t)strlen(entity.title),entity.title} :
                    CLAY_STRING("Unavailable shelf")),18,(Clay_Color){235,235,235,255});
            }
            (void)app_button(a,CLAY_IDI("ShelfMore",index),CLAY_STRING("View"),52,
                (AppAction){.type=APP_SELECT,.entity=entity},result != APP_OK || !page->items[index].available);
        }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(72)},.childGap=8}}) {
            unsigned columns = a->geometry.main >= 620 ? 3u : a->geometry.main >= 400 ? 2u : 1u;
            float width = (a->geometry.main-40-(columns-1)*8)/(float)columns;
            for (uint32_t j = 0; j < columns; ++j) {
                CollectionItem item; AppEntity card = {0};
                if (collection_shelf_item(page,index,j,&item) != COLLECTION_OK) { break; }
                bool valid = app_entity(&item,&card) == APP_OK;
                CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(72)},
                    .childGap=4,.layoutDirection=CLAY_TOP_TO_BOTTOM},.clip={.horizontal=true,.vertical=true}}) {
                    cover(a,&card,36);
                    (void)app_button(a,CLAY_IDI("HomeCard",index*4+j),valid ?
                        (Clay_String){false,(int32_t)strlen(card.title),card.title} : CLAY_STRING("Unavailable"),width,
                        (AppAction){.type=APP_SELECT,.entity=card},!valid || !item.available);
                }
            }
        }
    }
}
void app_home(App *a)
{
    UiListRange range;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("AppList"),&a->widgets.lists[0],a->model.page->count,
        112,fminf(336,a->geometry.viewport),0,key_at,shelf,a,&range);
    if (a->model.page->next != COLLECTION_END) {
        (void)app_button(a,CLAY_ID("AppMore"),CLAY_STRING("Load more"),112,
            (AppAction){.type=APP_MORE},a->model.pending);
    }
}
