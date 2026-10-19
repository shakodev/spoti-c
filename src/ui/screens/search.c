#include "ui/app.h"
#include "playlist.h"
#include <math.h>

void app_search(App *a)
{
    ui_text_input(&a->widgets,CLAY_ID("CatalogQuery"),&a->widgets.texts[0],fmaxf(64,a->geometry.main-48));
    if (a->control_count<APP_CONTROLS) { a->controls[a->control_count++]=(AppControl){.id=CLAY_ID("CatalogQuery"),.action={.type=APP_CATALOG_QUERY}}; }
    CLAY_AUTO_ID({.layout={.childGap=4}}) {
        const Clay_String labels[]={CLAY_STRING("Top"),CLAY_STRING("Tracks"),CLAY_STRING("Albums"),CLAY_STRING("Artists")};
        for (uint32_t i=0;i<4;++i) {
            (void)app_button(a,CLAY_IDI("CatalogCategory",i),labels[i],54,
                (AppAction){.type=APP_CATALOG_SECTION,.value=i},a->model.location.selector==i);
        }
    }
    if (!a->model.location.query[0] && a->model.logged_in && a->model.online) { app_label(CLAY_STRING("Search for music"),16,(Clay_Color){190,190,190,255}); }
    else { app_catalog_status(a); }
    if (a->model.logged_in) { app_catalog_rows(a); }
}
bool app_search_text_message(App *a, void *hwnd, uint32_t message, uintptr_t wp, intptr_t lp)
{
    return a->model.location.screen==APP_SEARCH && playlist_text_message(a,hwnd,message,wp,lp);
}
