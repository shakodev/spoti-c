#include "live_internal.h"
#include "api/json_base.h"
#include <string.h>
#include <wchar.h>
#include <stdio.h>

bool live_resolve(AppLive *h, ApEndpoints *endpoints, const CancelToken *cancel)
{
    HttpRequestId id; HttpCompletion completion={0};
    ApResult started=h->resolver_url ? ap_resolve_start_at(h->http,h->resolver_url,h->response,JSON_BASE_INPUT_LIMIT,&id) :
        ap_resolve_start(h->http,h->response,JSON_BASE_INPUT_LIMIT,&id);
    if (started!=AP_OK) {
        (void)snprintf(h->failure,sizeof(h->failure),"Spotify server discovery could not start (code %u).",(unsigned)started); return false;
    }
    bool cancelled=false;
    while (http_consume(h->http,id,&completion)==HTTP_PENDING) {
        if (!cancelled && !live_wait(h,cancel,http_wait_handle(h->http),http_next_wait_ms(h->http))) {
            cancelled=true; (void)http_cancel(h->http,id);
        }
        if (cancelled) { (void)http_wait(h->http,1000); } else { (void)http_pump(h->http); }
    }
    ApResult parsed=completion.final && completion.result==HTTP_OK && completion.status==200 ?
        ap_resolve_parse((char *)h->response,completion.bytes,JSON_BASE_INPUT_LIMIT+YYJSON_PADDING_SIZE,h->json_work,sizeof(h->json_work),endpoints) : AP_IO_ERROR;
    bool ok=!cancelled && parsed==AP_OK && endpoints->spclient_count && endpoints->spclient[0].port==443;
    if (!ok) { (void)snprintf(h->failure,sizeof(h->failure),"Spotify server discovery failed (code %u, HTTP %u).",(unsigned)parsed,completion.status); }
    return ok;
}
static void player_wake(void *context) { live_notify(context); }
static bool player_open(AppLive *h)
{
    Headless *player=NULL;
    HeadlessConfig config={.cache_directory=h->cache_path,.credential_path=h->credentials_path,
        .country=h->country,.catalogue="premium",.wasapi=true,.realtime=true,.state_changed=player_wake,.state_context=h};
    if (!headless_open(&config,&player)) { return false; }
    AcquireSRWLockExclusive(&h->player_lock); h->player=player; ReleaseSRWLockExclusive(&h->player_lock); return true;
}
static bool establish(AppLive *h, const char *access, const CancelToken *cancel)
{
    ApEndpoints endpoints; ApAuth auth={0}; ApSession *ap=NULL; bool ok=false;
    h->failure[0]=0;
    if (!live_resolve(h,&endpoints,cancel) || !arena_reset(&h->secret_arena) ||
        ap_auth_init(&auth,&h->credentials,access)!=AP_OK) { return false; }
    ApConfig config={.http=h->http,.version=124200290,.auth=&auth};
    if (ap_session_create(&h->secret_arena,&config,&ap)==AP_OK && ap_start(ap,&endpoints)==AP_OK) {
        uint64_t deadline=GetTickCount64()+90000;
        while (!worker_cancelled(cancel) && GetTickCount64()<deadline && ap_state(ap)!=AP_STATE_CLOSED) {
            if (ap_state(ap)==AP_STATE_AUTHENTICATED) { ok=ap_auth_result(&auth)==AP_OK; break; }
            HANDLE handles[6]; handles[0]=worker_cancel_handle(cancel); size_t n=ap_wait_handles(ap,handles+1,5);
            DWORD wait=ap_next_wait_ms(ap); if (wait>1000) { wait=1000; }
            if (WaitForMultipleObjects((DWORD)n+1,handles,FALSE,wait)==WAIT_FAILED) { break; }
            uint32_t gen=ap_generation(ap);
            (void)ap_on_event(ap,(ApEvent){AP_EVENT_TIMER,gen});
            (void)ap_on_event(ap,(ApEvent){ap_state(ap)==AP_STATE_RESOLVE ? AP_EVENT_HTTP : AP_EVENT_SOCKET,gen});
        }
        if (!ok) { (void)snprintf(h->failure,sizeof(h->failure),"Spotify connection failed (state %u, result %u, auth %u).",(unsigned)ap_state(ap),(unsigned)ap_result(ap),(unsigned)ap_auth_result(&auth)); }
        while (ap_session_shutdown(ap)==AP_PENDING) {
            HANDLE handles[5]; size_t n=ap_wait_handles(ap,handles,5);
            if (n) { (void)WaitForMultipleObjects((DWORD)n,handles,FALSE,1000); }
        }
    }
    ap_auth_clear(&auth);
    if (!ok || !h->credentials.ap_bytes || worker_cancelled(cancel)) {
        if (!h->failure[0]) { strcpy_s(h->failure,sizeof(h->failure),"Could not establish the encrypted Spotify session."); } return false;
    }
    wchar_t host[AP_HOST_CHARS];
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,endpoints.spclient[0].host,-1,host,AP_HOST_CHARS) ||
        swprintf_s(h->origin,256,L"https://%ls",host)<=0) { return false; }
    CredentialStore store={0};
    if (credential_store_save(&store,h->credentials_path,&h->credentials)!=CREDENTIAL_OK || !credential_store_balanced(&store)) {
        strcpy_s(h->failure,sizeof(h->failure),"Could not save the protected Spotify session."); return false;
    }
    if (http_client_shutdown(h->http,15000)!=HTTP_OK) { return false; } h->http=NULL;
    if (!live_http_open(h) || !arena_reset(&h->token_arena)) { return false; }
    typedef LONG (WINAPI *Version)(OSVERSIONINFOW *);
    FARPROC address=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion"); Version version;
    if (!address) { return false; } memcpy(&version,&address,sizeof(version));
    OSVERSIONINFOW os={.dwOSVersionInfoSize=sizeof(os)}; if (version(&os)) { return false; }
    TokenConfig tc={.http=h->http,.credentials=&h->credentials,.client_id=h->credentials.client_id,
        .client_version="1.2.42.290",.windows_major=os.dwMajorVersion,.windows_build=os.dwBuildNumber,
        .service_origins={h->origin,L"https://api-partner.spotify.com",L"https://spclient.wg.spotify.com"},.service_origin_count=3};
    if (token_manager_create(&h->token_arena,&tc,&h->tokens)!=TOKEN_OK) { return false; }
    h->authenticated=true; (void)player_open(h); return true;
}
bool live_account_restore(AppLive *h, LiveSlot *slot, const CancelToken *cancel)
{
    CredentialStore store={0}; CredentialResult r=credential_store_load(&store,h->credentials_path,&h->credentials);
    if (r==CREDENTIAL_NOT_FOUND) { slot->message[0]=0; return false; }
    if (r!=CREDENTIAL_OK || !credential_store_balanced(&store) || strcmp(h->client_id,h->credentials.client_id) || !h->credentials.ap_bytes) {
        strcpy_s(slot->message,sizeof(slot->message),"Saved session is unavailable. Please sign in again."); return false;
    }
    if (!live_http_open(h) || !establish(h,NULL,cancel)) {
        strcpy_s(slot->message,sizeof(slot->message),h->failure[0] ? h->failure : "Could not restore the Spotify session. Please sign in again."); return false;
    }
    return true;
}
bool live_account(AppLive *h, LiveSlot *slot, const CancelToken *cancel)
{
    OAuthFlow *flow=NULL; OAuthToken token={0}; bool ok=false;
    if (!live_account_close(h,false) || !live_http_open(h) || !arena_reset(&h->secret_arena) ||
        credential_record_init(&h->credentials,h->client_id)!=CREDENTIAL_OK) { return false; }
    OAuthConfig config={.client_id=h->client_id,.redirect_uri=h->redirect,.scopes=h->scopes};
    if (oauth_create(&h->secret_arena,&config,&flow)!=OAUTH_OK || oauth_begin(flow,true)!=OAUTH_OK) {
        strcpy_s(slot->message,sizeof(slot->message),"Could not open browser sign-in. Check OAuth client and redirect settings."); goto done;
    }
    while (!worker_cancelled(cancel) && oauth_state(flow)!=OAUTH_DONE && oauth_state(flow)!=OAUTH_TERMINAL) {
        if (oauth_state(flow)==OAUTH_CODE && oauth_exchange(flow,h->http)!=OAUTH_OK) { break; }
        HANDLE handles[4]; handles[0]=worker_cancel_handle(cancel); size_t n=oauth_wait_handles(flow,handles+1,3);
        DWORD wait=oauth_next_wait_ms(flow); if (wait>worker_cancel_wait(cancel)) { wait=worker_cancel_wait(cancel); }
        if (WaitForMultipleObjects((DWORD)n+1,handles,FALSE,wait)==WAIT_FAILED) { break; }
        (void)oauth_poll(flow);
    }
    ok=!worker_cancelled(cancel) && oauth_result(flow)==OAUTH_OK && oauth_take_token(flow,&h->credentials,&token)==OAUTH_OK;
    if (!ok && !worker_cancelled(cancel)) { (void)snprintf(slot->message,sizeof(slot->message),"Spotify token exchange failed (OAuth %u). Please sign in again.",(unsigned)oauth_result(flow)); }
done:
    if (flow) {
        (void)oauth_cancel(flow);
        while (oauth_shutdown(flow)==OAUTH_PENDING) { (void)http_wait(h->http,1000); }
        ok=oauth_balanced(flow)&&ok;
    }
    if (ok) {
        ok=establish(h,token.access,cancel);
        if (!ok && h->failure[0]) { strcpy_s(slot->message,sizeof(slot->message),h->failure); }
    }
    SecureZeroMemory(&token,sizeof(token));
    if (!ok && !slot->message[0] && !worker_cancelled(cancel)) {
        strcpy_s(slot->message,sizeof(slot->message),"Spotify sign-in failed or expired. Check your connection and account access, then retry.");
    }
    return ok;
}
bool live_account_close(AppLive *h, bool logout)
{
    bool ok=true; Headless *player;
    AcquireSRWLockExclusive(&h->player_lock); player=h->player; h->player=NULL; ReleaseSRWLockExclusive(&h->player_lock);
    if (player) {
        HeadlessMetrics metrics; ok=headless_close(player,&metrics);
    }
    if (h->tokens) {
        while (token_manager_shutdown(h->tokens)==TOKEN_PENDING) { (void)http_wait(h->http,1000); }
        ok=token_balanced(h->tokens)&&ok; h->tokens=NULL;
    }
    if (h->http) {
        bool closed=http_client_shutdown(h->http,15000)==HTTP_OK;
        if (closed) { h->http=NULL; } ok=closed&&ok;
    }
    if (logout) {
        CredentialStore store={0}; CredentialResult r=credential_store_logout(&store,h->credentials_path,&h->credentials);
        ok=(r==CREDENTIAL_OK || r==CREDENTIAL_NOT_FOUND)&&credential_store_balanced(&store)&&ok;
        ok=track_disk_close(&h->disk)&&ok;
        if (GetFileAttributesW(h->index_path)!=INVALID_FILE_ATTRIBUTES) { ok=DeleteFileW(h->index_path)!=0&&ok; }
        SecureZeroMemory(h->response,sizeof(h->response)); SecureZeroMemory(h->json_work,sizeof(h->json_work));
    } else { credential_record_clear(&h->credentials); }
    h->authenticated=false; return ok;
}
