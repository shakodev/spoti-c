#ifndef SPOTIC_LIVE_INTERNAL_H
#define SPOTIC_LIVE_INTERNAL_H
#include "app_live.h"
#include "live_wire.h"
#include "api/oauth.h"
#include "api/token_manager.h"
#include "protocol/ap_auth.h"
#include "protocol/ap_session.h"
#include "headless/headless.h"
#include "track_disk.h"
#include "screens/playlist.h"
#include "core/memory_budget.h"
#define LIVE_SLOTS 4u
typedef struct LiveSlot {
    AppRequest request;
    const CollectionPage *page;
    const CatalogPage *catalog_page;
    const LyricsDocument *lyrics;
    CollectionResult result;
    CatalogResult catalog_result;
    LyricsResult lyrics_result;
    TrackSource source;
    uint64_t ticket;
    atomic_uint phase;
    bool auth, logout, restore, success, retirement;
    char message[192];
    char artwork[512];
} LiveSlot;
struct AppLive {
    App *app; EventQueue *events; Worker *worker;
    LiveSlot slots[LIVE_SLOTS];
    SRWLOCK player_lock; Headless *player;
    atomic_uint_fast64_t retirement;
    bool stopping, initialized, authenticated, auth_waiting, logout_waiting, cleanup_success;
    AppRequest pending_auth;
    uint64_t generation;
    const wchar_t *request_version;
    const wchar_t *resolver_url; /* Controlled loopback test seam; NULL in production. */
    char failure[192];
    unsigned last_http_status;
    size_t last_http_bytes;
    wchar_t directory[230], credentials_path[230], cache_path[230], index_path[230], origin[256];
    char client_id[129], redirect[256], scopes[513], country[3];
    char timezone[96];
    CredentialRecord credentials;
    HttpClient *http; TokenManager *tokens;
    Arena http_arena, secret_arena, token_arena;
    _Alignas(uint64_t) unsigned char http_storage[MEMORY_BUDGET_HTTP];
    _Alignas(uint64_t) unsigned char secret_storage[MEMORY_BUDGET_PROTOCOL];
    _Alignas(uint64_t) unsigned char token_storage[TOKEN_SECRET_LIMIT];
    _Alignas(uint64_t) unsigned char pool_storage[MEMORY_BUDGET_API_PAGES];
    _Alignas(uint64_t) unsigned char collection_storage[COLLECTION_MODEL_BUDGET];
    unsigned char response[256u*1024u], json_work[128u*1024u];
    JsonPagePool *pool; CollectionService *collections; CatalogService *catalog;
    LyricsService *lyrics;
    TrackFilter filter; Playlist playlist; TrackDisk disk;
    LiveList list;
};
void live_notify(AppLive *);
bool live_http_open(AppLive *);
bool live_resolve(AppLive *, ApEndpoints *, const CancelToken *);
bool live_wait(AppLive *, const CancelToken *, HANDLE, DWORD);
bool live_account(AppLive *, LiveSlot *, const CancelToken *);
bool live_account_close(AppLive *, bool logout);
bool live_account_restore(AppLive *, LiveSlot *, const CancelToken *);
bool live_authorize(AppLive *, const wchar_t *, TokenAuthorization *, const CancelToken *);
bool live_http(AppLive *, const wchar_t *, HttpMethod, const void *, size_t, const wchar_t *, void *, size_t, HttpCompletion *, const CancelToken *, bool);
void live_request(AppLive *, LiveSlot *, const CancelToken *);
bool live_playlist(AppLive *, LiveSlot *, const CancelToken *);
bool live_list_fetch(AppLive *, const char *, uint32_t, const CancelToken *);
bool live_library_body(AppLive *, CollectionRequest *, size_t *);
bool live_saved_home_body(AppLive *, CollectionRequest *, bool section, size_t *);
bool live_home_body(AppLive *, const AppRequest *, const CollectionRequest *, char *, size_t, size_t *);
bool live_track_metadata(AppLive *, TrackRecord *, uint32_t, const CancelToken *);
bool live_metadata_decode(const void *, size_t, TrackRecord *, uint32_t);
void live_lyrics_request(AppLive *, LiveSlot *, const CancelToken *);
#endif
