#ifndef SPOTIC_UI_APP_H
#define SPOTIC_UI_APP_H
#include "widgets.h"
#include "font.h"
#include "artwork.h"
#include "icons.h"
#include "ui_scheduler.h"
#include "api/collection_api.h"
#include "audio/player.h"
#include "rootlist_page.h"
struct Playlist;
struct PlaylistQueue;
struct MediaDisplay;

#define APP_HISTORY 16u
#define APP_ACTIONS 32u
#define APP_CONTROLS 68u /* Six-column browse + retained sidebar + queue + two active card affordances. */
#define APP_IMAGES 20u
#define APP_INPUTS 16u
#define APP_MEASURES 192u /* Trade replaceable word advances for bounded visible controls/images. */
#include "app_result.h"
#include "app_notice.h"
#include "screens/catalog_view.h"
#include "app_browse.h"
typedef enum AppScreen { APP_HOME, APP_LIBRARY, APP_SETTINGS, APP_SHELF, APP_PLAYLIST,
    APP_SEARCH, APP_ALBUM, APP_ARTIST, APP_LYRICS } AppScreen;
typedef enum AppStatus { APP_LOADING, APP_CONTENT, APP_EMPTY, APP_ERROR,
    APP_AUTH_EXPIRED, APP_OFFLINE, APP_SIGNED_OUT } AppStatus;
typedef struct AppEntity { CollectionItemKind kind; char uri[192], title[192]; uint8_t id[16]; bool playable; } AppEntity;
typedef struct AppLocation { AppScreen screen; AppEntity entity; double scroll; uint32_t selector; char query[APP_QUERY_BYTES]; bool grid; } AppLocation;
typedef struct AppRequest { uint64_t generation, serial; uint32_t cursor; CollectionKind kind; char uri[192];
    AppScreen screen; uint32_t selector; char query[APP_QUERY_BYTES]; bool sidebar; } AppRequest;
typedef enum AppAuth { APP_LOGIN, APP_LOGOUT } AppAuth;
/* Callbacks run on the UI preparation boundary, NEVER layout/render. They must
 * enqueue bounded work to their service owner and return immediately. request
 * copies its descriptor; cancel retires its buffers on that owner. No native RPC
 * is silently converted to HTTP. publish delivery using app_deliver on UI owner.
 * auth owner invokes existing OAuth/token/credential APIs; report session state
 * with app_session. No credential data belongs to App. */
typedef struct AppServices {
    AppResult (*request)(void *, const AppRequest *);
    AppResult (*sidebar)(void *, const AppRequest *); /* Existing rootlist, independently retained. */
    AppResult (*catalog)(void *, const AppRequest *, JsonString artwork);
    void (*cancel)(void *, const AppRequest *);
    void (*retire)(void *, uint64_t generation); /* Enqueue owner-side route-reference retirement. */
    AppResult (*auth)(void *, AppAuth);
    PlayerResult (*submit)(void *, const PlayerCommand *);
    PlayerSnapshot (*snapshot)(void *);
    bool (*track_info)(void *, const uint8_t id[16], struct MediaDisplay *);
    bool (*queue_snapshot)(void *, PlayerQueueSnapshot *);
    PlayerResult (*refresh_clock)(void *); /* Nonblocking owner sample on timer wakes. */
    void (*wake)(void *);
    void *context;
    Player *player; /* Optional direct thread-safe existing Player binding. */
    Artwork *artwork; /* Borrowed UI-owned #28 service, outlives App. */
    struct Playlist *playlist; /* Shared bounded model; owner outlives cancelled jobs. */
    PlayerResult (*playlist_queue)(void *, const struct PlaylistQueue *); /* Copies IDs+revision synchronously. */
    bool demo; /* Explicit synthetic/offline host, visibly identified in title. */
} AppServices;
typedef enum AppActionType { APP_GO_HOME, APP_GO_LIBRARY, APP_GO_SETTINGS, APP_BACK,
    APP_RETRY, APP_MORE, APP_SIGN_IN, APP_SIGN_OUT, APP_SELECT, APP_TOGGLE_PLAY,
    APP_PREVIOUS, APP_NEXT, APP_SEEK, APP_VOLUME, APP_DETAIL,
    APP_TRACK_QUERY, APP_TRACK_SORT, APP_TRACK_PLAY, APP_TRACK_WINDOW,
    APP_GO_SEARCH, APP_GO_LYRICS, APP_CATALOG_QUERY, APP_CATALOG_SECTION,
    APP_CATALOG_SELECT, APP_LYRICS_FOLLOW, APP_MUTE, APP_READ_MESSAGES,
    APP_LIBRARY_GRID, APP_LIBRARY_FILTER, APP_LIBRARY_QUERY, APP_SIDEBAR_TOGGLE, APP_LIBRARY_FOCUS,
    APP_SHUFFLE, APP_REPEAT, APP_QUEUE, APP_PLAYER_INFO, APP_QUEUE_PAGE,
    APP_PROFILE_TOGGLE, APP_PROFILE_CLOSE, APP_SIDEBAR_MORE, APP_SIDEBAR_RETRY,
    APP_SIDEBAR_QUERY, APP_SIDEBAR_FOCUS, APP_SIDEBAR_SELECT, APP_BROWSE_PLAY } AppActionType;
typedef struct AppAction { AppActionType type; AppEntity entity; double value; uint64_t generation; } AppAction;
typedef struct AppControl { Clay_ElementId id; AppAction action; bool disabled; } AppControl;
enum { APP_CUSTOM_IMAGE=0x41525431, APP_CUSTOM_ICON=0x49434f31, APP_CUSTOM_SIDEBAR=0x53494431 };
typedef struct AppSidebarCover { uint32_t tag, index; ArtworkId id; ArtResult result; } AppSidebarCover;
typedef struct AppSidebar {
    const AppRootlistPage *page; /* Independent immutable finite rootlist projection. */
    uint64_t generation, serial;
    uint32_t cursor, cover_count, visible_count, visible[10], previous_count, previous[10];
    uint16_t order[COLLECTION_ROW_CAP], count, surrogate;
    AppSidebarCover covers[10];
    bool pending, failed, profile;
} AppSidebar;
typedef struct AppImage { uint32_t tag; AppEntity entity; ArtworkId id; ArtworkHandle handle; ArtResult result; } AppImage;
typedef struct AppGlyph { uint32_t tag; UiIcon icon; float color[4]; } AppGlyph;
typedef struct AppTrack {
    AppEntity entity;
    char artist[192], artwork[512];
    uint64_t used;
} AppTrack;
typedef struct AppNow {
    AppTrack tracks[8]; /* ID-keyed, independent of route/page ownership. */
    AppImage cover;
    PlayerQueueSnapshot queue;
    AppGlyph glyphs[24];
    uint64_t serial;
    uint32_t glyph_count, queue_page;
    const char *tooltip;
    Clay_ElementId tooltip_id;
    bool queue_open;
} AppNow;
typedef struct AppMeasure { FontConfig config; Clay_Dimensions dimensions; char text[128]; uint32_t length; } AppMeasure;
typedef struct AppGeometry { float width, height, sidebar, main, viewport, detail, bar, scale; bool compact; } AppGeometry;

typedef struct AppVolume {
    float value, nonzero, observed;
    uint64_t serial, pending_ticket, observed_ticket;
    bool observed_valid, restore_pending;
} AppVolume;
typedef struct AppModel {
    AppLocation location, history[APP_HISTORY];
    uint32_t history_count;
    uint64_t generation, serial, auth_serial;
    AppRequest request;
    const CollectionPage *page; /* ONE owned immutable delivery reference. */
    PlayerSnapshot player;
    AppVolume volume;
    AppEntity selected, playing;
    char playing_artwork[512];
    char playing_artist[192];
    uint8_t playing_artist_id[16];
    uint8_t playing_artwork_id[16];
    AppStatus status;
    AppResult last_result;
    char notice[APP_NOTICE_BYTES]; /* Public diagnostic only; never credentials. */
    AppSeverity notice_severity;
    bool logged_in, online, pending, detail, auth_pending;
} AppModel;
typedef struct App {
    const struct StateCache *cached; /* Borrowed startup snapshot; never authorizes playback. */
    AppModel model;
    AppBrowse browse;
    AppSidebar sidebar;
    AppNow now;
    CatalogView catalog;
    /* Fixed route-controller storage charged to UI state, used exclusively by
     * the bound serialized network owner; it outlives its cancellation ack. */
    _Alignas(uint64_t) unsigned char catalog_storage[80u*1024u];
    _Alignas(uint64_t) unsigned char lyrics_storage[18u*1024u];
    AppServices services;
    UiWidgets widgets;
    UiScheduler *scheduler;
    AppAction actions[APP_ACTIONS];
    AppControl controls[APP_CONTROLS];
    AppImage images[APP_IMAGES];
    UiWidgetInput inputs[APP_INPUTS];
    uint32_t action_count, control_count, image_count, input_count;
    char labels[64][192];
    uint32_t label_count;
    AppMeasure measures[APP_MEASURES]; /* Bounded replaceable word-advance cache. */
    uint32_t measure_count, measure_next;
    uint64_t measure_generation;
    uint16_t measure_dpi;
    AppGeometry geometry;
    AppMessages messages;
    FontMeasureContext *fonts; /* Optional; caller prepares pending text. */
    Clay_RenderCommandArray commands;
    char time[64], volume[40];
    uint64_t frames, stale_deliveries;
    bool alive;
} App;
_Static_assert(sizeof(App) <= 256u * 1024u, "combined app + widget state budget");
/* UI-thread only. No copies while live. Services and pools outlive disposal.
 * Once initialized, bind prepared font cache with app_bind_fonts if supplied.
 * App storage outlives workers bound to its route-controller buffers. */
AppResult app_init(App *, const AppServices *, UiScheduler *, void *clay, size_t clay_bytes,
    void *scratch, size_t scratch_bytes);
void app_bind_fonts(App *, FontMeasureContext *);
void app_dispose(App *);
AppResult app_geometry(float physical_width, float physical_height, float dpi, bool detail, AppGeometry *);
AppResult app_enqueue(App *, AppAction);
AppResult app_update(App *, uint64_t now); /* Bounded preparation/service dispatch. */
AppResult app_session(App *, bool logged_in, bool online, uint64_t now);
AppResult app_refresh(App *); /* Preparation only; retire generation and reload route. */
/* Consumes the owned delivery reference on ALL outcomes, including stale/error.
 * The producer must first release its own service reference on reset/disposal. */
AppResult app_deliver(App *, AppRequest, CollectionResult, const CollectionPage *);
AppResult app_publish_player(App *, PlayerSnapshot, uint64_t now);
void app_refresh_clock(App *, uint32_t dirty);
Clay_RenderCommandArray app_layout(App *, float physical_width, float physical_height, float dpi, UiWidgetInput);
bool app_button(App *, Clay_ElementId, Clay_String, float width, AppAction, bool disabled);
bool app_track_control(App *, Clay_ElementId, Clay_String, float width, AppAction, bool disabled, bool current);
void app_label(Clay_String, uint16_t size, Clay_Color);
Clay_String app_text(App *, Clay_String); /* Frame-owned stable text copy. */
AppResult app_entity(const CollectionItem *, AppEntity *);
AppResult app_image(App *, uint64_t generation, const AppEntity *, ArtworkId,
    const ArtworkSource *, size_t count); /* Safe preparation only. */
UiRenderResult app_render_custom(void *, UiRenderFrame *, const Clay_RenderCommand *);
typedef enum AppKey { APP_KEY_TAB, APP_KEY_ENTER, APP_KEY_BACK, APP_KEY_UP, APP_KEY_DOWN,
    APP_KEY_HOME, APP_KEY_END, APP_KEY_LEFT, APP_KEY_RIGHT, APP_KEY_PAGE_UP, APP_KEY_PAGE_DOWN, APP_KEY_ESCAPE } AppKey;
AppResult app_key(App *, AppKey, bool shift);
AppResult app_browse_key(App *, AppKey, const AppControl *);
AppResult app_pointer(App *, UiWidgetInput);
void app_scroll(App *, double dips);
/* Screen composition entry points, invoked within app_layout only. */
void app_sidebar(App *);
void app_sidebar_prepare(App *);
void app_sidebar_end(App *);
void app_sidebar_clear(App *);
void app_sidebar_cancel(App *);
AppResult app_sidebar_request(App *, uint32_t cursor);
AppResult app_sidebar_deliver(App *, AppRequest, CollectionResult, const AppRootlistPage *);
AppResult app_sidebar_key(App *, AppKey, const AppControl *);
void app_profile_menu(App *);
UiWidgetInput app_profile_input(App *, UiWidgetInput);
void app_home(App *);
void app_library(App *);
void app_now_playing(App *);
void app_now_prepare(App *);
AppResult app_track_remember(App *, const AppEntity *, const char *artist, const char *artwork);
void app_player_icon(App *, Clay_ElementId, UiIcon, AppAction, const char *label,
    const char *unavailable, bool selected, bool primary);
void app_player_tooltip(App *);
void app_queue_panel(App *);
void app_volume_init(App *);
bool app_volume_available(const App *);
void app_volume_snapshot(App *, PlayerSnapshot);
void app_volume_prepare(App *);
void app_volume_session_end(App *);
AppResult app_volume_set(App *, double value);
AppResult app_volume_key(App *, AppKey);
void app_volume_controls(App *); /* Layout only. */
void app_playlist_prepare(App *); /* Preparation only, existing player handoff. */
#endif
