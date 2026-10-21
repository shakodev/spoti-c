#include "ui/app.h"
#include <math.h>
#include <string.h>

bool app_catalog_cards(const App *a)
{
    return (a->model.location.screen==APP_SEARCH && (a->model.location.selector==SEARCH_ALBUMS ||
        a->model.location.selector==SEARCH_ARTISTS)) || (a->model.location.screen==APP_ARTIST &&
        a->model.location.selector!=ARTIST_TOP_TRACKS);
}
uint32_t app_catalog_columns(const App *a)
{
    return a->geometry.main>=820 ? 4u : a->geometry.main>=610 ? 3u : a->geometry.main>=400 ? 2u : 1u;
}
float app_catalog_card_height(const App *a)
{
    float width=(a->geometry.main-40)/app_catalog_columns(a);
    return fminf(128,width-16)+84;
}
static AppImage *image(App *a, const AppEntity *entity)
{
    for (uint32_t i=0;i<a->image_count;++i) {
        if (!strcmp(a->images[i].entity.uri,entity->uri)) { return &a->images[i]; }
    }
    return NULL;
}
static void card(App *a, uint32_t index, float width, float cover)
{
    const CatalogItem *item=&a->catalog.page->items[index]; AppEntity entity={0};
    bool enabled=app_catalog_entity(item,&entity)==APP_OK && !a->model.pending;
    Clay_ElementId id=CLAY_IDI("CatalogCard",index);
    AppAction action={.type=APP_CATALOG_SELECT,.value=index};
    if (a->control_count==APP_CONTROLS) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return; }
    a->controls[a->control_count++]=(AppControl){id,action,!enabled};
    Clay_String subtitle=item->kind==CATALOG_ARTIST ? CLAY_STRING("Artist") : CLAY_STRING("Album");
    UiCardContent content={.title=app_text(a,(Clay_String){false,(int32_t)strlen(entity.title),entity.title}),
        .subtitle=enabled ? subtitle : CLAY_STRING("Unavailable"),.artwork=image(a,&entity),.round=item->kind==CATALOG_ARTIST};
    if (ui_component_card(&a->widgets,id,content,width,cover,!enabled,a->widgets.lists[0].selection==app_catalog_key(a,index))) {
        a->widgets.lists[0].selection=a->widgets.lists[0].focus=app_catalog_key(a,index);
        (void)app_enqueue(a,action);
    }
    ++a->catalog.rendered;
}
static uint64_t key(void *context, uint32_t row)
{
    App *a=context; return app_catalog_key(a,row*app_catalog_columns(a));
}
static void row(void *context, uint32_t row_index, uint64_t key_value)
{
    (void)key_value; App *a=context; uint32_t columns=app_catalog_columns(a);
    float width=(a->geometry.main-40)/columns, cover=fminf(128,width-16);
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(cover+84)},.childGap=4}}) {
        for (uint32_t col=0;col<columns;++col) {
            uint32_t index=row_index*columns+col;
            if (index>=a->catalog.page->count) { break; }
            card(a,index,width,cover);
        }
    }
}
void app_catalog_grid(App *a)
{
    UiVirtualList *list=&a->widgets.lists[1]; list->scroll=a->widgets.lists[0].scroll;
    UiListRange range; uint32_t columns=app_catalog_columns(a); a->catalog.rendered=0;
    (void)ui_virtual_list(&a->widgets,CLAY_ID("CatalogList"),list,(a->catalog.page->count+columns-1)/columns,
        app_catalog_card_height(a),app_catalog_viewport(a),0,key,row,a,&range);
    a->widgets.lists[0].scroll=list->scroll;
    if (a->control_count<APP_CONTROLS) { a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("CatalogList"),
        .action={.type=APP_CATALOG_SELECT,.value=-1},.disabled=a->model.pending}; }
}
bool app_catalog_grid_key(App *a, AppKey key_value)
{
    uint32_t at=0, count=a->catalog.page->count, columns=app_catalog_columns(a);
    if (!count) { return false; }
    (void)app_catalog_index(a,a->widgets.lists[0].focus,&at);
    switch (key_value) {
    case APP_KEY_LEFT: if (at) { --at; } break;
    case APP_KEY_RIGHT: if (at+1<count) { ++at; } break;
    case APP_KEY_UP: at=at>=columns ? at-columns : 0; break;
    case APP_KEY_DOWN: at=at+columns<count ? at+columns : count-1; break;
    case APP_KEY_HOME: at=0; break;
    case APP_KEY_END: at=count-1; break;
    case APP_KEY_PAGE_UP: at=at>=columns*2 ? at-columns*2 : 0; break;
    case APP_KEY_PAGE_DOWN: at=at+columns*2<count ? at+columns*2 : count-1; break;
    default: return false;
    }
    UiVirtualList *list=&a->widgets.lists[0];
    list->selection=list->focus=app_catalog_key(a,at);
    double top=(at/columns)*app_catalog_card_height(a), viewport=app_catalog_viewport(a);
    if (top<list->scroll) { list->scroll=top; }
    else if (top+app_catalog_card_height(a)>list->scroll+viewport) {
        list->scroll=fmax(0,top+app_catalog_card_height(a)-viewport);
    }
    a->widgets.focus=CLAY_ID("CatalogList").id; return true;
}
void app_catalog_featured(App *a)
{
    if (!a->catalog.page || !a->catalog.page->count || a->model.status!=APP_CONTENT || a->model.pending) { return; }
    const CatalogItem *item=&a->catalog.page->items[0]; AppEntity entity={0};
    bool enabled=app_catalog_entity(item,&entity)==APP_OK;
    app_heading(CLAY_STRING("Top result"),24);
    CLAY(CLAY_ID("CatalogTopResult"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(88)},
        .padding=CLAY_PADDING_ALL(8),.childGap=16},.backgroundColor=UI_HIGHLIGHT,.cornerRadius=CLAY_CORNER_RADIUS(6)}) {
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(72),CLAY_SIZING_FIXED(72)}},
            .backgroundColor=UI_ELEVATED_HOVER,.cornerRadius=CLAY_CORNER_RADIUS(item->kind==CATALOG_ARTIST ? 36.0f : 4.0f),
            .custom={.customData=image(a,&entity)}}) { }
        CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(72)},.layoutDirection=CLAY_TOP_TO_BOTTOM,.childGap=8},
            .clip={.horizontal=true,.vertical=true}}) {
            (void)app_component(a,CLAY_ID("CatalogFeatured"),(Clay_String){false,(int32_t)strlen(entity.title),entity.title},
                fmaxf(80,a->geometry.main-152),32,(AppAction){.type=APP_CATALOG_SELECT,.value=0},!enabled,
                (UiButtonStyle){.kind=UI_BUTTON_ROW});
            app_label(item->kind==CATALOG_ARTIST ? CLAY_STRING("Artist") : item->kind==CATALOG_ALBUM ? CLAY_STRING("Album") :
                CLAY_STRING("Song"),14,UI_SUBDUED);
        }
    }
}
