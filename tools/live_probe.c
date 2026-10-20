/* Explicit account-dependent diagnostic. Prints no tokens, URLs, identities or
 * response bodies. Not registered in default CTest; never logs in/out, submits
 * player commands, opens WASAPI or mutates the account's Library. */
#include "ui/live_internal.h"
#include "ui/collection_artwork.h"
#include "live_capture.h"

#include "core/memory_budget.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "core/thread.h"

static _Alignas(16) unsigned char decode[ART_DECODE_BYTES], rgba[ART_UPLOAD_BYTES];
static void probe_candidate(MetadataSpan picture, unsigned ordinal, const CancelToken *cancel)
{
    MetadataReader reader={picture,MEDIA_OK,0}; uint32_t tag; pb_wire_type_t wire;
    MetadataSpan label={0},url={0};
    while (metadata_next(&reader,&tag,&wire)) {
        if (tag==1 || tag==2) {
            MetadataSpan value; if (!metadata_span(&reader,wire,&value)) { return; }
            if (tag==1) { label=value; } else { url=value; }
        } else if (!metadata_skip(&reader,wire)) { return; }
    }
    char target[65]="<opaque-label>";
    bool plain=label.bytes && label.bytes<sizeof(target);
    for (size_t i=0;i<label.bytes && plain;++i) {
        unsigned c=label.data[i];
        plain=(c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-';
    }
    if (plain) { memcpy(target,label.data,label.bytes); target[label.bytes]=0; }
    bool https=url.bytes>=8 && !memcmp(url.data,"https://",8);
    printf("Supplied picture candidate %u: target %s; HTTPS %u; URL bytes %zu.\n",
        ordinal,target,(unsigned)https,url.bytes);
    if (!https || url.bytes>=ART_URL_CHARS || memchr(url.data,0,url.bytes)) { return; }
    wchar_t wide[ART_URL_CHARS]; int chars=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
        (const char *)url.data,(int)url.bytes,wide,ART_URL_CHARS-1);
    if (chars<=0) { return; } wide[chars]=0;
    size_t bytes=0; uint32_t width=0,height=0; ImageStats stats={0};
    ArtResult fetched=artwork_http_fetch(NULL,wide,decode,ART_COMPRESSED_BYTES,&bytes,cancel), decoded=ART_PENDING;
    if (fetched==ART_OK) { decoded=image_decode(decode,decode,bytes,rgba,128,cancel,&width,&height,&stats); }
    printf("Candidate sample: fetch %u; bytes %zu; decode %u; dimensions %u x %u.\n",
        (unsigned)fetched,bytes,(unsigned)decoded,width,height);
    SecureZeroMemory(decode,sizeof(decode)); SecureZeroMemory(rgba,sizeof(rgba));
}
/* Inspect at most eight supplied variants on the first decorated row. The
 * nesting is from the retained playlist_api.proto descriptor, not a URL rule. */
static bool probe_picture_fields(MetadataSpan span, unsigned depth, const CancelToken *cancel)
{
    const uint32_t path[]={5,4,2,13};
    MetadataReader reader={span,MEDIA_OK,0}; uint32_t tag, count=0; pb_wire_type_t wire;
    while (metadata_next(&reader,&tag,&wire)) {
        if (tag==path[depth]) {
            MetadataSpan child; if (!metadata_span(&reader,wire,&child)) { return false; }
            if (depth<3) { if (probe_picture_fields(child,depth+1,cancel)) { return true; } }
            else {
                if (count==LIVE_IMAGE_CANDIDATES) { break; }
                probe_candidate(child,++count,cancel);
            }
        } else if (!metadata_skip(&reader,wire)) { return false; }
    }
    return count!=0;
}

static void probe_images(const CollectionPage *page, const CancelToken *cancel)
{
    unsigned candidates=0, rejected=0; CollectionArtwork image;
    wchar_t first[ART_URL_CHARS]={0};
    for (uint32_t i=0;i<page->count;++i) {
        bool shelf=page->kind==COLLECTION_HOME;
        for (uint32_t j=0;j<(shelf ? COLLECTION_ROW_CAP : 1);++j) {
            CollectionItem item;
            if (shelf) {
                if (collection_shelf_item(page,i,j,&item)!=COLLECTION_OK) { break; }
            } else { item=page->items[i]; }
            AppResult result=app_collection_artwork_read(&item,&image);
            if (result==APP_OK) {
                ++candidates;
                if (!first[0]) { wcscpy_s(first,ART_URL_CHARS,image.sources[0].url); }
            } else if (result!=APP_UNAVAILABLE) { ++rejected; }
        }
    }
    printf("Artwork metadata: %u usable rows; %u rejected rows.\n",candidates,rejected);
    if (first[0]) {
        size_t bytes=0; Metrics metrics; bool measured=metrics_init(&metrics);
        ArtResult fetched=artwork_http_fetch(measured ? &metrics : NULL,first,decode,ART_COMPRESSED_BYTES,&bytes,cancel);
        ArtResult decoded=ART_PENDING; uint32_t width=0,height=0; ImageStats stats={0};
        if (fetched==ART_OK) { decoded=image_decode(decode,decode,bytes,rgba,128,cancel,&width,&height,&stats); }
        printf("Artwork sample: fetch %u; bytes %zu; decode %u; dimensions %u x %u; balanced %u.\n",
            (unsigned)fetched,bytes,(unsigned)decoded,width,height,(unsigned)(measured && metrics_balanced(&metrics) && !stats.live_objects));
        SecureZeroMemory(decode,sizeof(decode)); SecureZeroMemory(rgba,sizeof(rgba));
    }
}

int main(int argc, char **argv)
{
    bool variants=false,capture=false,matrix=false,reference=false;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--image-variants")) { variants=true; }
        else if (!strcmp(argv[i],"--capture")) { capture=true; }
        else if (!strcmp(argv[i],"--capture-matrix")) { matrix=true; }
        else if (!strcmp(argv[i],"--capture-reference")) { reference=true; }
        else if (!strcmp(argv[i],"--help")) {
            puts("Usage: spotic-live-probe [--image-variants] [--capture] [--capture-matrix] [--capture-reference]"); return 0;
        } else { fputs("Usage: spotic-live-probe [--image-variants] [--capture] [--capture-matrix] [--capture-reference]\n",stderr); return 2; }
    }
    /* This diagnostic is a dedicated non-UI process/service owner. */
    core_thread_register_background();
    AppLive *h=VirtualAlloc(NULL,sizeof(*h),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if (!h) { return 1; } InitializeSRWLock(&h->player_lock);
    const AppRootlistPage *sidebar=NULL;
    bool all_ok=true;
    bool ok=arena_init(&h->http_arena,h->http_storage,sizeof(h->http_storage)) &&
        arena_init(&h->secret_arena,h->secret_storage,sizeof(h->secret_storage)) &&
        arena_init(&h->token_arena,h->token_storage,sizeof(h->token_storage));
    wchar_t path[230]; DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",path,190);
    CredentialStore store={0};
    if (!n || n>=190) { ok=false; } else {
        (void)swprintf_s(h->cache_path,230,L"%ls/spoti-c/audio",path);
        (void)swprintf_s(h->index_path,230,L"%ls/spoti-c/probe-%lu.index",path,GetCurrentProcessId());
        wcscat_s(path,230,L"/spoti-c/credentials.dat");
    }
    if (ok) { wcscpy_s(h->credentials_path,230,path); }
    if (ok) { ok=credential_store_load(&store,path,&h->credentials)==CREDENTIAL_OK; }
    CancelToken cancel={.deadline=CORE_CLOCK_NEVER}; atomic_init(&cancel.requested,false);
    cancel.wake=CreateEventW(NULL,TRUE,FALSE,NULL); ok=ok && cancel.wake;
    ApEndpoints endpoints;
    if (ok) { ok=live_http_open(h) && live_resolve(h,&endpoints,&cancel); }
    if (!ok) { puts(h->failure[0] ? h->failure : "No saved session or service initialization failed."); goto done; }
    wchar_t host[AP_HOST_CHARS];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,endpoints.spclient[0].host,-1,host,AP_HOST_CHARS) ||
        swprintf_s(h->origin,256,L"https://%ls",host)<=0 || http_client_shutdown(h->http,15000)!=HTTP_OK) { ok=false; goto done; }
    h->http=NULL; if (!live_http_open(h)) { ok=false; goto done; }
    typedef LONG (WINAPI *Version)(OSVERSIONINFOW *);
    FARPROC address=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"); Version version;
    if (!address) { ok=false; goto done; } memcpy(&version,&address,sizeof(version));
    OSVERSIONINFOW os={.dwOSVersionInfoSize=sizeof(os)}; if (version(&os)) { ok=false; goto done; }
    TokenConfig config={.http=h->http,.credentials=&h->credentials,.client_id=h->credentials.client_id,
        .client_version="1.2.42.290",.windows_major=os.dwMajorVersion,.windows_build=os.dwBuildNumber,
        .service_origins={h->origin,L"https://api-partner.spotify.com"},.service_origin_count=2};
    ok=token_manager_create(&h->token_arena,&config,&h->tokens)==TOKEN_OK;
    if (ok) {
        TokenAuthorization authorization={0};
        ok=live_authorize(h,L"https://api-partner.spotify.com/pathfinder/v2/query",&authorization,&cancel);
        token_authorization_clear(&authorization);
        puts(ok ? "Internal-service authorization accepted." : h->failure);
        printf("Client token present: %u; service bearer present: %u; result: %u.\n",
            (unsigned)token_info(h->tokens,TOKEN_CLIENT).present,(unsigned)token_info(h->tokens,TOKEN_BEARER).present,(unsigned)token_result(h->tokens));
    }
    if (ok) {
        ok=json_page_pool_init(h->pool_storage,sizeof(h->pool_storage),&h->pool)==JSON_OK &&
            collection_service_init(h->collection_storage,sizeof(h->collection_storage),h->pool,&h->collections)==COLLECTION_OK;
        if (ok) { h->rootlists=app_rootlist_store(h->collection_storage,sizeof(h->collection_storage)); ok=h->rootlists!=NULL; }
        strcpy_s(h->timezone,sizeof(h->timezone),"Europe/Amsterdam"); h->authenticated=true;
        if (ok && (capture || matrix || reference)) {
            LiveSlot rootlist={.request={.kind=COLLECTION_LIBRARY,.screen=APP_LIBRARY,.generation=1,.serial=1,.sidebar=true}};
            live_request(h,&rootlist,&cancel); sidebar=rootlist.rootlist;
            bool accepted=rootlist.result==COLLECTION_OK && sidebar;
            printf("Retained live sidebar: accepted %u; loaded playlist rows %u; finite source-row cap %u.\n",
                (unsigned)accepted,sidebar ? sidebar->count : 0,APP_ROOTLIST_ROWS);
            all_ok=accepted && all_ok;
        }
        for (unsigned i=0;ok && i<2;++i) {
            LiveSlot slot={.request={.kind=i ? COLLECTION_LIBRARY : COLLECTION_HOME,.screen=i ? APP_LIBRARY : APP_HOME,.generation=i+1}};
            live_request(h,&slot,&cancel);
            printf("%s result: %u; rows: %u; public diagnostic: %s\n",i ? "Library" : "Home",(unsigned)slot.result,
                slot.page ? slot.page->count : 0,slot.message[0] ? slot.message : slot.page ? "accepted" : "unavailable");
            printf("HTTP status: %u; response bytes: %zu.\n",h->last_http_status,h->last_http_bytes);
            if (i) {
                unsigned https=0,opaque=0,missing=0;
                for (uint32_t row=0;row<h->list.count;++row) {
                    const char *image=h->list.rows[row].image_uri;
                    if (!image[0]) { ++missing; }
                    else if (!_strnicmp(image,"https://",8)) { ++https; }
                    else { ++opaque; }
                }
                printf("Rootlist image fields: HTTPS %u; opaque %u; missing %u.\n",https,opaque,missing);
                if (variants) { (void)probe_picture_fields((MetadataSpan){h->response,h->last_http_bytes},0,&cancel); }
            }
            all_ok=(slot.result==COLLECTION_OK || slot.result==COLLECTION_PARTIAL || slot.result==COLLECTION_BOUNDARY)&&all_ok;
            if (slot.page) {
                probe_images(slot.page,&cancel);
                if (capture) {
                    bool captured=live_capture_page_at(slot.page,slot.request,slot.result,slot.message,
                        "build/live-captures",i ? "library-list" : "home",false,(LiveCaptureViewport){1100,900,96},sidebar);
                    if (i && captured) {
                        captured=live_capture_page_at(slot.page,slot.request,slot.result,slot.message,
                            "build/live-captures","library-grid",true,(LiveCaptureViewport){1100,900,96},sidebar);
                    }
                    all_ok=captured && all_ok;
                }
                if (matrix) {
                    bool captured=live_capture_matrix(slot.page,slot.request,slot.result,slot.message,
                        "build/live-captures",i ? "library" : "home",sidebar);
                    all_ok=captured && all_ok;
                }
                if (reference) {
                    all_ok=live_capture_reference(slot.page,slot.request,slot.result,slot.message,
                        "build/live-captures",i ? "library" : "home",sidebar) && all_ok;
                }
                (void)page_release((JsonPage *)&slot.page->json);
            }
            (void)collection_reset(h->collections,i+2);
        }
    }
    if (ok) {
        LiveList library=h->list;
        for (uint32_t i=0;i<library.count;++i) {
            if (strncmp(library.rows[i].uri,"spotify:playlist:",17)) { continue; }
            bool fetched=live_list_fetch(h,library.rows[i].uri,0,&cancel);
            all_ok=fetched && all_ok;
            printf("Playlist page accepted: %u; HTTP %u; rows %u; total %u.\n",(unsigned)fetched,h->last_http_status,h->list.count,h->list.total);
            if (fetched) {
                TrackRecord rows[LIVE_ROWS]={0};
                for (uint32_t j=0;j<h->list.count;++j) { if (media_gid_from_uri(h->list.rows[j].uri,rows[j].id)==MEDIA_OK) { rows[j].flags=TRACK_ID|TRACK_PLAYABLE; } }
                bool metadata_ok=live_track_metadata(h,rows,h->list.count,&cancel); unsigned available=0;
                for (uint32_t j=0;j<h->list.count;++j) { if (rows[j].flags&TRACK_METADATA) { ++available; } }
                printf("Playlist metadata accepted: %u; hydrated rows %u; diagnostic: %s\n",(unsigned)metadata_ok,available,h->failure);
                LiveSlot indexed={0}; strcpy_s(indexed.request.uri,sizeof(indexed.request.uri),library.rows[i].uri);
                bool index_ok=live_playlist(h,&indexed,&cancel);
                printf("Playlist disk index accepted: %u; records %u; diagnostic: %s\n",(unsigned)index_ok,indexed.source.total,indexed.message);
                all_ok=index_ok && metadata_ok && all_ok;
                (void)track_disk_close(&h->disk); (void)DeleteFileW(h->index_path);
            }
            break;
        }
    }
done:
    ok=live_capture_shutdown(15000)&&ok;
    app_rootlist_release(sidebar);
    if (h->pool) { (void)json_page_pool_dispose(h->pool); }
    ok=live_account_close(h,false)&&ok&&all_ok;
    if (cancel.wake) { (void)CloseHandle(cancel.wake); }
    arena_dispose(&h->http_arena); arena_dispose(&h->secret_arena); arena_dispose(&h->token_arena);
    SecureZeroMemory(h,sizeof(*h)); (void)VirtualFree(h,0,MEM_RELEASE); return ok ? 0 : 1;
}
