#include "app.h"
#include <math.h>

AppResult app_browse_key(App *a, AppKey key, const AppControl *control)
{
    app_browse_project(a);
    uint32_t count=a->browse.count;
    if (!count) { return APP_CANCELLED; }
    UiVirtualList *list=&a->widgets.lists[0]; uint32_t offset=a->model.page->offset;
    if (control->action.value>=0) { list->focus=(uint64_t)offset+(uint32_t)control->action.value+1; }
    uint32_t position=0;
    while (position<count && (uint64_t)offset+a->browse.order[position]+1!=list->focus) { ++position; }
    if (position==count) { position=0; }
    uint32_t columns=a->model.location.grid ? app_browse_columns(a) : 1;
    float width=(a->geometry.main-32-(columns-1)*12)/(float)columns;
    double height=a->model.location.grid ? width+52 : 64;
    double viewport=fmin(a->model.location.grid ? 2*height : 384,app_browse_viewport(a,true));
    uint32_t step=(uint32_t)fmax(1,floor(viewport/height))*columns;
    uint32_t next=position;
    switch (key) {
    case APP_KEY_HOME: next=0; break;
    case APP_KEY_END: next=count-1; break;
    case APP_KEY_UP: next=position>=columns ? position-columns : 0; break;
    case APP_KEY_DOWN: next=position+columns<count ? position+columns : count-1; break;
    case APP_KEY_LEFT: next=position ? position-1 : 0; break;
    case APP_KEY_RIGHT: next=position+1<count ? position+1 : count-1; break;
    case APP_KEY_PAGE_UP: next=position>=step ? position-step : 0; break;
    case APP_KEY_PAGE_DOWN: next=position+step<count ? position+step : count-1; break;
    case APP_KEY_ENTER: {
        const CollectionItem *item=&a->model.page->items[a->browse.order[position]]; AppEntity entity;
        if (!item->available || app_entity(item,&entity)!=APP_OK) { return APP_CANCELLED; }
        return app_enqueue(a,(AppAction){.type=APP_SELECT,.entity=entity});
    }
    default: return APP_CANCELLED;
    }
    list->focus=list->selection=(uint64_t)offset+a->browse.order[next]+1;
    double top=(next/columns)*height;
    if (viewport<height || top<list->scroll) { list->scroll=top; }
    else if (top+height>list->scroll+viewport) { list->scroll=fmax(0,top+height-viewport); }
    a->widgets.focus=CLAY_ID("AppList").id; return APP_OK;
}
