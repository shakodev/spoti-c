#include "app.h"
#include <bcrypt.h>
#include <math.h>
#include <string.h>

static void wake(App *a)
{
    if (a->scheduler) { ui_scheduler_dirty(a->scheduler,UI_DIRTY_STATE); }
    if (a->services.wake) { a->services.wake(a->services.context); }
}
void app_sidebar_cancel(App *a)
{
    AppSidebar *s=&a->sidebar;
    if (s->pending && a->services.cancel) {
        AppRequest r={.generation=s->generation,.serial=s->serial,.cursor=s->cursor,
            .kind=COLLECTION_LIBRARY,.screen=APP_LIBRARY,.sidebar=true};
        a->services.cancel(a->services.context,&r);
    }
    s->pending=false; s->cover_count=s->previous_count=s->visible_count=0;
}
void app_sidebar_clear(App *a)
{
    app_sidebar_cancel(a);
    app_rootlist_release(a->sidebar.page);
    memset(&a->sidebar,0,sizeof(a->sidebar));
    a->widgets.lists[1]=(UiVirtualList){0}; (void)ui_text_set(&a->widgets.texts[2],"",0);
}
AppResult app_sidebar_request(App *a,uint32_t cursor)
{
    AppSidebar *s=&a->sidebar;
    if (s->pending) { return APP_PENDING; }
    if (!a->model.logged_in || !a->model.online || !a->services.sidebar) { return APP_UNAVAILABLE; }
    if (a->model.serial==UINT64_MAX) { return APP_FULL; }
    AppRequest r={.generation=a->model.generation,.serial=++a->model.serial,.cursor=cursor,
        .kind=COLLECTION_LIBRARY,.screen=APP_LIBRARY,.sidebar=true};
    AppResult result=a->services.sidebar(a->services.context,&r);
    s->generation=r.generation; s->serial=r.serial; s->cursor=cursor;
    s->pending=result==APP_PENDING; s->failed=!s->pending;
    return result;
}
AppResult app_sidebar_deliver(App *a,AppRequest r,CollectionResult result,const AppRootlistPage *page)
{
    AppResult out=APP_STALE;
    if (a && a->alive && a->model.logged_in && a->sidebar.pending && r.sidebar &&
        r.generation==a->model.generation && r.generation==a->sidebar.generation && r.serial==a->sidebar.serial &&
        r.kind==COLLECTION_LIBRARY && r.cursor==a->sidebar.cursor) {
        AppSidebar *s=&a->sidebar; s->pending=false; s->failed=true; out=APP_FAILED;
        if ((result==COLLECTION_OK || result==COLLECTION_PARTIAL || result==COLLECTION_BOUNDARY) && page &&
            page->generation==r.generation && page->offset==r.cursor &&
            page->count<=APP_ROOTLIST_ROWS && (page->next==COLLECTION_END || page->next>r.cursor)) {
            app_rootlist_release(s->page);
            s->page=page; page=NULL; s->failed=false; s->cover_count=s->previous_count=s->visible_count=0;
            a->widgets.lists[1]=(UiVirtualList){0}; out=APP_OK;
        }
        wake(a);
    }
    app_rootlist_release(page);
    if (out==APP_STALE && a && a->alive) { ++a->stale_deliveries; }
    return out;
}
void app_sidebar_end(App *a)
{
    AppSidebar *s=&a->sidebar;
    if (s->previous_count==s->visible_count && !memcmp(s->previous,s->visible,s->visible_count*sizeof(s->visible[0]))) { return; }
    memcpy(s->previous,s->visible,s->visible_count*sizeof(s->visible[0])); s->previous_count=s->visible_count;
    if (a->services.artwork) { wake(a); }
}
void app_sidebar_prepare(App *a)
{
    AppSidebar *s=&a->sidebar;
    if (!s->page && !s->pending && !s->failed && a->model.logged_in && a->model.online && a->services.sidebar) {
        (void)app_sidebar_request(a,0);
    }
    s->cover_count=0;
    if (!s->page || !a->model.logged_in || !a->services.artwork) { return; }
    for (uint32_t i=0;i<s->previous_count && s->cover_count<10;++i) {
        uint32_t index=s->previous[i];
        if (index>=s->page->count) { continue; }
        const char *url=s->page->items[index].image; wchar_t wide[512];
        size_t bytes=strnlen_s(url,512);
        if (bytes<8 || _strnicmp(url,"https://",8) ||
            !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,url,-1,wide,512)) { continue; }
        AppSidebarCover *cover=&s->covers[s->cover_count++];
        *cover=(AppSidebarCover){.tag=APP_CUSTOM_SIDEBAR,.index=index,
            .id={.size=(uint32_t)fminf(256,ceilf(48*a->geometry.scale)),.generation=a->model.generation}};
        if (!cover->id.size) { cover->id.size=48; }
        if (BCryptHash(BCRYPT_SHA1_ALG_HANDLE,NULL,0,(PUCHAR)url,(ULONG)bytes,cover->id.content,sizeof(cover->id.content))<0) {
            --s->cover_count; continue;
        }
        ArtworkSource source={wide,0,0};
        cover->result=artwork_request(a->services.artwork,cover->id,&source,1);
    }
}
static uint64_t key_at(void *context,uint32_t i)
{
    App *a=context; return (uint64_t)a->sidebar.order[i]+1;
}
static bool index_of(void *context,uint64_t key,uint32_t *index)
{
    App *a=context;
    for (uint32_t i=0;i<a->sidebar.count;++i) { if (key_at(a,i)==key) { *index=i; return true; } }
    return false;
}
AppResult app_sidebar_key(App *a,AppKey key,const AppControl *control)
{
    UiVirtualList *list=&a->widgets.lists[1];
    if (control->action.value>=0) { list->focus=(uint64_t)control->action.value+1; }
    UiNav nav;
    switch (key) {
    case APP_KEY_UP: nav=UI_NAV_UP; break; case APP_KEY_DOWN: nav=UI_NAV_DOWN; break;
    case APP_KEY_HOME: nav=UI_NAV_HOME; break; case APP_KEY_END: nav=UI_NAV_END; break;
    case APP_KEY_PAGE_UP: nav=UI_NAV_PAGE_UP; break; case APP_KEY_PAGE_DOWN: nav=UI_NAV_PAGE_DOWN; break;
    default: {
        if (key!=APP_KEY_ENTER || !list->focus || !a->sidebar.page || list->focus>a->sidebar.page->count) { return APP_CANCELLED; }
        AppAction action={.type=APP_SIDEBAR_SELECT,.value=(double)(list->focus-1)};
        CollectionItem item=app_rootlist_item(a->sidebar.page,(uint32_t)(list->focus-1));
        if (app_entity(&item,&action.entity)!=APP_OK) { return APP_INVALID; }
        return app_enqueue(a,action);
    }
    }
    (void)ui_virtual_navigate(list,a->sidebar.count,64,fmin(640,fmax(64,a->geometry.height-332)),nav,true,key_at,index_of,a);
    a->widgets.focus=CLAY_ID("SidebarList").id; wake(a); return APP_OK;
}
