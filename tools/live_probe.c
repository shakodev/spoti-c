/* Explicit account-dependent diagnostic. Prints no tokens, URLs, identities or
 * response bodies. Not registered in default CTest and never logs in/out. */
#include "ui/live_internal.h"

#include "core/memory_budget.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "core/thread.h"

static void audio_changed(void *context) { (void)SetEvent(context); }
static bool probe_audio(AppLive *h, const uint8_t id[16])
{
    HANDLE changed=CreateEventW(NULL,FALSE,FALSE,NULL); if (!changed) { return false; }
    HeadlessConfig config={.cache_directory=h->cache_path,.credential_path=h->credentials_path,.country="NL",.catalogue="premium",
        .wasapi=true,.realtime=true,.state_changed=audio_changed,.state_context=changed};
    Headless *player=NULL; bool accepted=false;
    if (headless_open(&config,&player)) {
        PlayerCommand command={.type=PLAYER_LOAD,.play=true}; memcpy(command.track_id,id,16);
        bool loaded=headless_submit(player,&command)==PLAYER_OK; uint64_t deadline=GetTickCount64()+30000;
        PlayerState previous=(PlayerState)-1;
        while (loaded && GetTickCount64()<deadline) {
            PlayerSnapshot p=headless_snapshot(player);
            if (p.state!=previous) { printf("Physical playback state %u; audio result %u.\n",(unsigned)p.state,(unsigned)p.error); fflush(stdout); previous=p.state; }
            if (p.state==PLAYER_ERROR) { break; }
            if (p.state==PLAYER_PLAYING && p.rate && p.timeline_valid && p.position_frames>=(uint64_t)p.rate*3) { accepted=true; break; }
            (void)WaitForSingleObject(changed,1000); (void)headless_refresh_clock(player);
        }
        HeadlessMetrics metrics; accepted=headless_close(player,&metrics)&&accepted;
    }
    (void)CloseHandle(changed); printf("Physical WASAPI consumed-timeline playback accepted: %u.\n",(unsigned)accepted); return accepted;
}
int main(void)
{
    /* This diagnostic is a dedicated non-UI process/service owner. */
    core_thread_register_background();
    AppLive *h=VirtualAlloc(NULL,sizeof(*h),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if (!h) { return 1; } InitializeSRWLock(&h->player_lock);
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
        strcpy_s(h->timezone,sizeof(h->timezone),"Europe/Amsterdam"); h->authenticated=true;
        for (unsigned i=0;ok && i<2;++i) {
            LiveSlot slot={.request={.kind=i ? COLLECTION_LIBRARY : COLLECTION_HOME,.screen=i ? APP_LIBRARY : APP_HOME,.generation=i+1}};
            live_request(h,&slot,&cancel);
            printf("%s result: %u; rows: %u; public diagnostic: %s\n",i ? "Library" : "Home",(unsigned)slot.result,
                slot.page ? slot.page->count : 0,slot.page ? "accepted" : slot.message);
            printf("HTTP status: %u; response bytes: %zu.\n",h->last_http_status,h->last_http_bytes);
            all_ok=(slot.result==COLLECTION_OK || slot.result==COLLECTION_PARTIAL || slot.result==COLLECTION_BOUNDARY)&&all_ok;
            if (slot.page) { (void)page_release((JsonPage *)&slot.page->json); }
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
                if (GetEnvironmentVariableW(L"SPOTIC_PROBE_AUDIO",NULL,0) && available) { all_ok=probe_audio(h,rows[0].id)&&all_ok; }
            }
            break;
        }
    }
done:
    if (h->pool) { (void)json_page_pool_dispose(h->pool); }
    ok=live_account_close(h,false)&&ok&&all_ok;
    if (cancel.wake) { (void)CloseHandle(cancel.wake); }
    arena_dispose(&h->http_arena); arena_dispose(&h->secret_arena); arena_dispose(&h->token_arena);
    SecureZeroMemory(h,sizeof(*h)); (void)VirtualFree(h,0,MEM_RELEASE); return ok ? 0 : 1;
}


