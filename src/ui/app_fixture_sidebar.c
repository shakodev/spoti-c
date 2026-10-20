#include "app_fixture.h"
#include "api/collection_internal.h"
#include <limits.h>
#include <stdio.h>
#include <wchar.h>

void app_fixture_sidebar_run(AppFixture *h,AppFixtureSlot *slot,const CancelToken *cancel)
{
    CollectionPage input={.kind=COLLECTION_LIBRARY,.offset=slot->request.cursor};
    atomic_init(&input.json.refcount,UINT_MAX);
    slot->result=COLLECTION_ERROR;
    if (json_page_acquire(h->pool,&input.json,slot->request.generation)!=JSON_OK) { slot->result=COLLECTION_FULL; return; }
    wchar_t path[512]; FILE *file=NULL;
    const wchar_t *name=slot->request.cursor ? L"library-next.json" : L"library.json";
    bool ok=swprintf_s(path,512,L"%ls/%ls",h->directory,name)>0 && _wfopen_s(&file,path,L"rb")==0 && file;
    size_t bytes=0;
    if (ok) { bytes=fread(input.json.input,1,JSON_PAGE_BODY_CAP,file); ok=!ferror(file) && fgetc(file)==EOF; (void)fclose(file); }
    ok=ok && !worker_cancelled(cancel) && json_page_parse(&input.json,bytes)==JSON_OK &&
        collection_library_parse(&input,yyjson_doc_get_root(input.json.doc),(JsonString){0})==COLLECTION_OK;
    if (ok) {
        AppRootlistPage *page=app_rootlist_begin(h->rootlists,slot->request.generation,slot->request.cursor,input.total);
        if (page) {
            uint32_t consumed=input.count<APP_ROOTLIST_ROWS ? input.count : APP_ROOTLIST_ROWS;
            for (uint32_t i=0;i<consumed;++i) {
                const CollectionItem *item=&input.items[i];
                if (item->kind!=COLLECTION_PLAYLIST_ITEM) { continue; }
                JsonString image={0}; yyjson_val *info=yyjson_obj_get(item->source,"entityInfo");
                (void)json_view_string(info,"imageUri",&image);
                (void)app_rootlist_append(page,item->uri,item->name,image,item->available);
            }
            app_rootlist_publish(page,consumed<input.count ? slot->request.cursor+consumed : input.next);
            slot->rootlist=page; slot->result=COLLECTION_OK;
        } else { slot->result=COLLECTION_FULL; }
    }
    json_page_discard(&input.json);
}
