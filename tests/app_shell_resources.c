#include "app_shell_support.h"
#include "ui/app_account.h"

static _Alignas(16) unsigned char secret_memory[MEMORY_BUDGET_PROTOCOL];
void shell_account(void)
{
    Arena secrets={0}; OAuthFlow *oauth=NULL;
    CHECK(arena_init(&secrets,secret_memory,sizeof(secret_memory)));
    OAuthConfig config={.client_id="fixture-client",.redirect_uri="http://127.0.0.1/callback",.scopes="streaming"};
    CHECK(oauth_create(&secrets,&config,&oauth)==OAUTH_OK);
    CredentialStore store={0}; CredentialRecord record;
    CHECK(credential_record_init(&record,"fixture-client")==CREDENTIAL_OK);
    wchar_t directory[MAX_PATH], path[MAX_PATH]; CHECK(GetTempPathW(MAX_PATH,directory));
    CHECK(GetTempFileNameW(directory,L"sac",0,path));
    AppAccount owner={.oauth=oauth,.store=&store,.credentials=&record,.credential_path=path};
    CHECK(app_account_action(&owner,APP_LOGIN)==APP_PENDING);
    CHECK(oauth_state(oauth)==OAUTH_WAITING);
    CHECK(app_account_action(&owner,APP_LOGOUT)==APP_OK);
    CHECK(!record.client_id[0] && oauth_balanced(oauth) && credential_store_balanced(&store));
    CHECK(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES);
    CHECK(oauth_shutdown(oauth)==OAUTH_OK); arena_dispose(&secrets);
    puts("account owner: existing PKCE begin/cancel and credential logout passed without browser/account");
}
static HANDLE started;
static ArtResult fetch(void *context, const wchar_t *url, void *buffer, size_t capacity, size_t *bytes, const CancelToken *cancel)
{
    (void)context; (void)url; (void)buffer; (void)capacity; *bytes=0; CHECK(SetEvent(started));
    CHECK(WaitForSingleObject(worker_cancel_handle(cancel),5000)==WAIT_OBJECT_0); return ART_CANCELLED;
}
static ArtResult upload(void *context, uint32_t w, uint32_t h, const unsigned char *rgba, UiTexture *out)
{
    (void)context; (void)w; (void)h; (void)rgba; (void)out; CHECK(false); return ART_INVALID;
}
static void release(void *context, UiTexture texture) { (void)context; (void)texture; CHECK(false); }
static bool valid(void *context, UiTexture texture) { (void)context; (void)texture; return false; }
static _Alignas(16) unsigned char art_storage[65536], workspace[ART_DECODE_BYTES];
static unsigned char pixels[ART_UPLOAD_BYTES], core_memory[MEMORY_BUDGET_CORE];
void shell_artwork(void)
{
    DWORD before,after; CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    Arena core={0}; Worker *worker=NULL; Artwork *art=NULL;
    CHECK(arena_init(&core,core_memory,sizeof(core_memory)) && worker_start(&core,4,(CoreClock){0},&worker)==CORE_OK);
    started=CreateEventW(NULL,TRUE,FALSE,NULL); CHECK(started);
    ArtworkConfig config={.worker=worker,.fetch=fetch,.gpu={upload,release,valid,NULL},.decode_workspace=workspace,.upload=pixels};
    CHECK(artwork_size()<=sizeof(art_storage)); CHECK(artwork_init(art_storage,sizeof(art_storage),&config,&art)==ART_OK);
    shell_open(NULL); app.services.artwork=art;
    CHECK(app_session(&app,true,true,0)==APP_PENDING); shell_deliver("home");
    AppEntity entity={.uri="fixture:art",.title="Public fixture"};
    ArtworkId id={.size=128}; id.content[0]=1;
    ArtworkSource source={L"https://i.scdn.co/image/public-fixture",128,128};
    CHECK(app_image(&app,app.model.generation,&entity,id,&source,1)==APP_PENDING);
    CHECK(app_image(&app,app.model.generation,&entity,id,&source,1)==APP_PENDING && app.image_count==1);
    uint64_t old=app.model.generation; (void)app_update(&app,0);
    CHECK(WaitForSingleObject(started,5000)==WAIT_OBJECT_0);
    shell_action(APP_GO_LIBRARY); CHECK(app.image_count==0);
    CHECK(app_image(&app,old,&entity,id,&source,1)==APP_STALE);
    CHECK(WaitForSingleObject(artwork_wait_handle(art),5000)==WAIT_OBJECT_0);
    (void)app_update(&app,0); CHECK(artwork_stats(art).cancelled==1 && artwork_stats(art).gpu_bytes==0);
    shell_close();
    CHECK(artwork_stop(art)==ART_OK); CHECK(worker_stop(worker)==CORE_OK && worker_join(worker,5000)==CORE_OK);
    CHECK(CloseHandle(started)); arena_dispose(&core);
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    puts("artwork navigation: request coalescing, generation cancellation, stale rejection and handle balance passed");
}
