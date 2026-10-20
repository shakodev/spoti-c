#include "rootlist_page.h"
#include <limits.h>
#include <string.h>

AppRootlistStore *app_rootlist_store(void *storage,size_t bytes)
{
    size_t offset=(collection_service_size()+_Alignof(AppRootlistStore)-1)&~(size_t)(_Alignof(AppRootlistStore)-1);
    if (!storage || (uintptr_t)storage%_Alignof(AppRootlistStore) || offset>bytes || sizeof(AppRootlistStore)>bytes-offset) { return NULL; }
    AppRootlistStore *store=(AppRootlistStore *)((unsigned char *)storage+offset);
    memset(store,0,sizeof(*store));
    for (unsigned i=0;i<2;++i) { atomic_init(&store->pages[i].references,0); }
    return store;
}
AppRootlistPage *app_rootlist_begin(AppRootlistStore *store,uint64_t generation,uint32_t cursor,uint32_t total)
{
    if (!store || !generation) { return NULL; }
    for (unsigned i=0;i<2;++i) {
        AppRootlistPage *page=&store->pages[i]; unsigned expected=0;
        if (!atomic_compare_exchange_strong_explicit(&page->references,&expected,UINT_MAX,memory_order_acq_rel,memory_order_acquire)) { continue; }
        page->generation=generation; page->offset=cursor; page->total=total; page->next=COLLECTION_END; page->count=0;
        memset(page->items,0,sizeof(page->items)); return page;
    }
    return NULL;
}
bool app_rootlist_append(AppRootlistPage *page,JsonString uri,JsonString name,JsonString image,bool available)
{
    if (!page || page->count==APP_ROOTLIST_ROWS || atomic_load(&page->references)!=UINT_MAX) { return false; }
    AppRootlistItem item={.available=available};
    if (json_base_string_copy(uri,item.uri,sizeof(item.uri))!=JSON_OK ||
        json_base_string_copy(name,item.name,sizeof(item.name))!=JSON_OK) { return false; }
    /* Unsupported/overlong supplied covers keep an honest generic cover. */
    if (json_base_string_copy(image,item.image,sizeof(item.image))!=JSON_OK) { item.image[0]=0; }
    page->items[page->count++]=item; return true;
}
void app_rootlist_publish(AppRootlistPage *page,uint32_t next)
{
    page->next=next; atomic_store_explicit(&page->references,1,memory_order_release);
}
void app_rootlist_release(const AppRootlistPage *page)
{
    if (page) { (void)atomic_fetch_sub_explicit(&((AppRootlistPage *)page)->references,1,memory_order_acq_rel); }
}
CollectionItem app_rootlist_item(const AppRootlistPage *page,uint32_t index)
{
    if (!page || index>=page->count) { return (CollectionItem){0}; }
    const AppRootlistItem *item=&page->items[index];
    return (CollectionItem){.kind=COLLECTION_PLAYLIST_ITEM,.available=item->available,
        .uri={item->uri,strlen(item->uri)},.name={item->name,strlen(item->name)}};
}
