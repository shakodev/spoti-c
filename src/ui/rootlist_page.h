#ifndef SPOTIC_ROOTLIST_PAGE_H
#define SPOTIC_ROOTLIST_PAGE_H
#include "api/collection_api.h"
#define APP_ROOTLIST_ROWS 16u
typedef struct AppRootlistItem { char uri[192],name[192],image[512]; bool available; } AppRootlistItem;
typedef struct AppRootlistPage {
    atomic_uint references;
    uint64_t generation;
    uint32_t offset,next,total,count;
    AppRootlistItem items[APP_ROOTLIST_ROWS];
} AppRootlistPage;
typedef struct AppRootlistStore { AppRootlistPage pages[2]; } AppRootlistStore;
/* Two immutable finite projections share the existing 64KiB model reservation
 * with CollectionService. No JSON-page reservation survives publication. */
AppRootlistStore *app_rootlist_store(void *storage,size_t bytes);
AppRootlistPage *app_rootlist_begin(AppRootlistStore *,uint64_t generation,uint32_t cursor,uint32_t total);
bool app_rootlist_append(AppRootlistPage *,JsonString uri,JsonString name,JsonString image,bool available);
void app_rootlist_publish(AppRootlistPage *,uint32_t next);
void app_rootlist_release(const AppRootlistPage *);
CollectionItem app_rootlist_item(const AppRootlistPage *,uint32_t index);
#endif
