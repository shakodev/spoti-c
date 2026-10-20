#include "live_internal.h"
#include <string.h>
#include <wchar.h>

static bool write_json(yyjson_mut_doc *doc, const yyjson_alc *allocator, char *out, size_t cap, size_t *bytes)
{
    yyjson_write_err error={0}; char *text=yyjson_mut_write_opts(doc,0,allocator,bytes,&error);
    bool ok=text && *bytes<cap;
    if (ok) { memcpy(out,text,*bytes); out[*bytes]=0; }
    yyjson_mut_doc_free(doc); return ok;
}
bool live_home_body(AppLive *h, const AppRequest *r, const CollectionRequest *request, char *out, size_t cap, size_t *bytes)
{
    yyjson_alc allocator; yyjson_alc_pool_init(&allocator,h->json_work,sizeof(h->json_work));
    yyjson_mut_doc *doc=yyjson_mut_doc_new(&allocator); if (!doc) { return false; }
    yyjson_mut_val *root=yyjson_mut_obj(doc), *vars=yyjson_mut_obj(doc), *ext=yyjson_mut_obj(doc), *persisted=yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc,root);
    bool ok=root && vars && ext && persisted;
    ok=ok && yyjson_mut_obj_add_str(doc,vars,"homeEndUserIntegration","INTEGRATION_DESKTOP") &&
        yyjson_mut_obj_add_str(doc,vars,"timeZone",h->timezone) && yyjson_mut_obj_add_str(doc,vars,"sp_t","") &&
        yyjson_mut_obj_add_bool(doc,vars,"includeEpisodeContentRatingsV2",true) &&
        yyjson_mut_obj_add_uint(doc,vars,"sectionItemsLimit",r->kind==COLLECTION_HOME ? 10 : request->limit);
    if (r->kind==COLLECTION_HOME) { ok=ok && yyjson_mut_obj_add_str(doc,vars,"facet",""); }
    else {
        ok=ok && yyjson_mut_obj_add_str(doc,vars,"uri",r->uri) &&
            yyjson_mut_obj_add_uint(doc,vars,"sectionItemsOffset",r->cursor);
    }
    ok=ok && yyjson_mut_obj_add_str(doc,root,"operationName",request->contract->wire_operation) &&
        yyjson_mut_obj_add_val(doc,root,"variables",vars) && yyjson_mut_obj_add_uint(doc,persisted,"version",1) &&
        yyjson_mut_obj_add_str(doc,persisted,"sha256Hash",request->contract->persisted_sha256) &&
        yyjson_mut_obj_add_val(doc,ext,"persistedQuery",persisted) && yyjson_mut_obj_add_val(doc,root,"extensions",ext);
    if (!ok) { yyjson_mut_doc_free(doc); return false; } return write_json(doc,&allocator,out,cap,bytes);
}
bool live_list_fetch(AppLive *h, const char *uri, uint32_t offset, const CancelToken *cancel)
{
    wchar_t url[HTTP_URL_CHARS], id[3072];
    if (uri) {
        static const char prefix[]="spotify:playlist:"; uint8_t gid[16];
        if (strncmp(uri,prefix,sizeof(prefix)-1) || strlen(uri)!=sizeof(prefix)-1+22 ||
            media_gid_from_base62(uri+sizeof(prefix)-1,gid)!=MEDIA_OK || !live_url_component(uri+sizeof(prefix)-1,id,3072)) { return false; }
        if (swprintf_s(url,HTTP_URL_CHARS,L"%ls/playlist/v2/playlist/%ls?from=%u&length=64&decorate=revision,attributes,length,owner,capabilities,status_code",h->origin,id,offset)<=0) { return false; }
    } else {
        if (!live_url_component(h->credentials.username,id,3072) ||
            swprintf_s(url,HTTP_URL_CHARS,L"%ls/playlist/v2/user/%ls/rootlist?from=%u&length=64&decorate=revision,attributes,length,owner,capabilities,status_code",h->origin,id,offset)<=0) { return false; }
    }
    HttpCompletion completion;
    return live_http(h,url,HTTP_GET,NULL,0,L"application/protobuf",h->response,sizeof(h->response),&completion,cancel,true) &&
        completion.status==200 && live_list_decode(h->response,completion.bytes,&h->list) &&
        (!h->list.contents || h->list.position==offset) && (!h->list.truncated || h->list.count!=0);
}
bool live_library_body(AppLive *h, CollectionRequest *r, size_t *bytes)
{
    yyjson_alc allocator; yyjson_alc_pool_init(&allocator,h->json_work,sizeof(h->json_work));
    yyjson_mut_doc *doc=yyjson_mut_doc_new(&allocator); if (!doc) { return false; }
    yyjson_mut_val *root=yyjson_mut_obj(doc), *header=yyjson_mut_obj(doc), *rows=yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc,root);
    bool ok=root && header && rows && yyjson_mut_obj_add_uint(doc,root,"statusCode",200) &&
        yyjson_mut_obj_add_uint(doc,header,"totalCount",h->list.total) &&
        yyjson_mut_obj_add_bool(doc,header,"isLoading",false) && yyjson_mut_obj_add_val(doc,root,"header",header) &&
        yyjson_mut_obj_add_val(doc,root,"entity",rows);
    for (uint32_t i=0;ok && i<h->list.count;++i) {
        LiveRow *item=&h->list.rows[i]; yyjson_mut_val *row=yyjson_mut_obj(doc), *info=yyjson_mut_obj(doc);
        bool playlist=strncmp(item->uri,"spotify:playlist:",17)==0;
        ok=row && info && yyjson_mut_obj_add_str(doc,info,"uri",item->uri) &&
            yyjson_mut_obj_add_str(doc,info,"name",item->name[0] ? item->name : item->uri) &&
            yyjson_mut_obj_add_val(doc,row,"entityInfo",info) && yyjson_mut_arr_append(rows,row);
        if (item->image_uri[0]) { ok=ok && yyjson_mut_obj_add_str(doc,info,"imageUri",item->image_uri); }
        if (playlist) { ok=ok && yyjson_mut_obj_add_val(doc,row,"playlist",yyjson_mut_obj(doc)); }
    }
    if (!ok) { yyjson_mut_doc_free(doc); return false; } return write_json(doc,&allocator,r->body,r->capacity,bytes);
}
static void catalog_request_run(AppLive *h, LiveSlot *slot, const CancelToken *cancel)
{
    if (!h->catalog) {
        CatalogConfig config={.locale={"en-GB",5},.album_limit=20,.debounce_ms=0,.image_width=64};
        if (catalog_service_init(h->app->catalog_storage,sizeof(h->app->catalog_storage),h->pool,&config,&h->catalog)!=CATALOG_OK) { return; }
        (void)catalog_reset(h->catalog,h->generation);
    }
    AppRequest *r=&slot->request; CatalogRequest request={0};
    if (r->screen==APP_SEARCH) {
        slot->catalog_result=search_request(h->catalog,(JsonString){r->query,strlen(r->query)},(SearchCategory)r->selector,r->cursor,20,r->generation,GetTickCount64(),&request);
    } else if (r->screen==APP_ALBUM) {
        slot->catalog_result=album_request(h->catalog,(JsonString){r->uri,strlen(r->uri)},r->cursor,r->generation,&request);
    } else {
        slot->catalog_result=artist_request(h->catalog,(JsonString){r->uri,strlen(r->uri)},(ArtistSection)r->selector,r->cursor,r->generation,&request);
    }
    if (slot->catalog_result!=CATALOG_PENDING || !request.ticket.serial) { return; }
    HttpCompletion completion={0};
    h->request_version=(r->screen==APP_ALBUM || r->screen==APP_ARTIST) ? L"896000000" : NULL;
    bool ok=live_http(h,L"https://api-partner.spotify.com/pathfinder/v2/query",HTTP_POST,request.upload,request.upload_bytes,
        L"application/json;charset=UTF-8",request.body,request.capacity,&completion,cancel,true);
    h->request_version=NULL;
    CatalogResult result=worker_cancelled(cancel) ? CATALOG_CANCELLED : ok ? CATALOG_OK : CATALOG_ERROR;
    slot->catalog_result=catalog_complete(h->catalog,request.ticket,result,completion.status,completion.bytes,
        (JsonString){request.contract->persisted_sha256,strlen(request.contract->persisted_sha256)},&slot->catalog_page);
}
static void sidebar_request(AppLive *h,LiveSlot *slot,const CancelToken *cancel)
{
    AppRequest *r=&slot->request;
    if (!live_list_fetch(h,NULL,r->cursor,cancel)) { slot->result=COLLECTION_ERROR; return; }
    AppRootlistPage *page=app_rootlist_begin(h->rootlists,r->generation,r->cursor,h->list.total);
    if (!page) { slot->result=COLLECTION_FULL; return; }
    uint32_t consumed=h->list.count<APP_ROOTLIST_ROWS ? h->list.count : APP_ROOTLIST_ROWS;
    for (uint32_t i=0;i<consumed;++i) {
        LiveRow *row=&h->list.rows[i];
        if (strncmp(row->uri,"spotify:playlist:",17)) { continue; }
        (void)app_rootlist_append(page,(JsonString){row->uri,strlen(row->uri)},
            (JsonString){row->name,strlen(row->name)},(JsonString){row->image_uri,strlen(row->image_uri)},true);
    }
    uint64_t next=(uint64_t)r->cursor+consumed;
    app_rootlist_publish(page,consumed && next<COLLECTION_END && (next<h->list.total || h->list.truncated) ? (uint32_t)next : COLLECTION_END);
    slot->rootlist=page; slot->result=worker_cancelled(cancel) ? COLLECTION_CANCELLED : COLLECTION_OK;
}
void live_request(AppLive *h, LiveSlot *slot, const CancelToken *cancel)
{
    AppRequest *r=&slot->request;
    if (r->sidebar) { sidebar_request(h,slot,cancel); return; }
    if (r->screen==APP_LYRICS) { live_lyrics_request(h,slot,cancel); return; }
    if (r->screen>=APP_SEARCH) { catalog_request_run(h,slot,cancel); return; }
    if (r->screen==APP_PLAYLIST) {
        slot->result=live_playlist(h,slot,cancel) ? COLLECTION_OK : COLLECTION_ERROR; return;
    }
    CollectionRequest request={0}; slot->result=collection_fetch(h->collections,r->kind,r->cursor,r->generation,&request);
    if (slot->result!=COLLECTION_PENDING || !request.ticket.serial) { return; }
    bool ok=false; size_t bytes=0; unsigned status=200;
    bool saved_shelf=r->kind==COLLECTION_HOME_SECTION && !strcmp(r->uri,"spoti-c:playlists");
    if (saved_shelf) {
        ok=live_list_fetch(h,NULL,r->cursor,cancel) && live_saved_home_body(h,&request,true,&bytes);
    } else
    if (r->kind==COLLECTION_LIBRARY) {
        ok=live_list_fetch(h,NULL,r->cursor,cancel) && live_library_body(h,&request,&bytes);
        strcpy_s(slot->message,sizeof(slot->message),"Your playlists. Saved albums, artists and folders are not included in this view.");
    } else {
        char upload[4096]; size_t upload_bytes=0; HttpCompletion completion={0};
        ok=live_home_body(h,r,&request,upload,sizeof(upload),&upload_bytes) &&
            live_http(h,L"https://api-partner.spotify.com/pathfinder/v2/query",HTTP_POST,upload,upload_bytes,
                L"application/json;charset=UTF-8",request.body,request.capacity,&completion,cancel,true);
        bytes=completion.bytes; status=completion.status;
        if (ok && status==403 && r->kind==COLLECTION_HOME && !worker_cancelled(cancel)) {
            ok=live_list_fetch(h,NULL,0,cancel) && live_saved_home_body(h,&request,false,&bytes);
            status=200;
            strcpy_s(slot->message,sizeof(slot->message),"Personalized Home is unavailable. Showing your playlists.");
        }
    }
    CollectionResult result=worker_cancelled(cancel) ? COLLECTION_CANCELLED : ok ? COLLECTION_OK : COLLECTION_ERROR;
    slot->result=collection_complete(h->collections,request.ticket,result,status,bytes,&slot->page);
    if (!ok) { strcpy_s(slot->message,sizeof(slot->message),h->failure[0] ? h->failure : "Could not decode Spotify content. Please retry."); }
}
