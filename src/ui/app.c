#include "app.h"
#include "startup.h"
#include "screens/playlist.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void failed(void *context, UiWidgetResult result)
{
    App *a = context; a->model.last_result = result == UI_WIDGET_FULL ? APP_FULL : APP_FAILED;
}
AppResult app_init(App *a, const AppServices *services, UiScheduler *scheduler,
    void *clay, size_t clay_bytes, void *scratch, size_t scratch_bytes)
{
    if (!a || !services) { return APP_INVALID; }
    memset(a,0,sizeof(*a)); a->services = *services; a->scheduler = scheduler;
    a->model.generation = 1; a->model.status = APP_SIGNED_OUT;
    app_catalog_clear(a);
    if (ui_widgets_init(&a->widgets,clay,clay_bytes,scratch,scratch_bytes,failed,a) != UI_WIDGET_OK) { return APP_FULL; }
    a->alive = true; app_volume_init(a); return APP_OK;
}
AppResult app_geometry(float width, float height, float dpi, bool detail, AppGeometry *out)
{
    if (!out || !isfinite(width) || !isfinite(height) || !isfinite(dpi) || dpi < 48 || dpi > 768 ||
        width <= 0 || height <= 0) { return APP_INVALID; }
    float w = width * 96 / dpi, h = height * 96 / dpi;
    if (w < 400 || h < 320 || w > 16384 || h > 16384) { return APP_FULL; }
    float sidebar = w < 760 ? 64.0f : 184.0f, panel = detail && w >= 1180 ? 240.0f : 0.0f;
    float extra = w < 760 ? 32.0f : 0;
    *out = (AppGeometry){.width=w,.height=h,.sidebar=sidebar,.detail=panel,.bar=88+extra,
        .main=w-24-sidebar-panel-(panel ? 8 : 0),.viewport=fmaxf(0,h-248-extra),.compact=w<760};
    return APP_OK;
}
void app_label(Clay_String text, uint16_t size, Clay_Color color)
{
    CLAY_TEXT(text,CLAY_TEXT_CONFIG({.fontSize=size,.textColor=color,.wrapMode=CLAY_TEXT_WRAP_NONE}));
}
bool app_button(App *a, Clay_ElementId id, Clay_String text, float width, AppAction action, bool disabled)
{
    if (a->control_count == APP_CONTROLS) { ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return false; }
    a->controls[a->control_count++] = (AppControl){id,action,disabled};
    bool hit = false;
    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(width),CLAY_SIZING_FIXED(28)}},.clip={.horizontal=true,.vertical=true}}) {
        hit = ui_button(&a->widgets,id,app_text(a,text),width,28,disabled);
    }
    if (hit) { (void)app_enqueue(a,action); } return hit;
}
Clay_String app_text(App *a, Clay_String text)
{
    if (a->label_count == 64 || text.length < 0 || text.length >= 192 || (text.length && !text.chars)) {
        ui_widgets_fail(&a->widgets,UI_WIDGET_FULL); return CLAY_STRING("Text exceeds capacity");
    }
    char *out = a->labels[a->label_count++];
    if (text.length) { memcpy(out,text.chars,(size_t)text.length); }
    out[text.length] = 0; return (Clay_String){false,text.length,out};
}
static void status(App *a)
{
    const char *s = a->model.status == APP_LOADING ? "Loading..." :
        a->model.status == APP_EMPTY ? "Nothing here yet." : a->model.status == APP_AUTH_EXPIRED ?
        "Your session expired. Sign in again." : a->model.status == APP_OFFLINE ?
        "You are offline." : a->model.status == APP_SIGNED_OUT ? "Sign in to see your music." :
        "Couldn't load this page. Try again.";
    app_message(a,(Clay_String){true,(int32_t)strlen(s),s},
        a->model.status==APP_ERROR || a->model.status==APP_AUTH_EXPIRED ? APP_FAILURE :
        a->model.status==APP_OFFLINE ? APP_WARNING : APP_INFORMATION);
    if (a->model.status == APP_ERROR || a->model.status == APP_OFFLINE) {
        (void)app_button(a,CLAY_ID("AppRetry"),CLAY_STRING("Try again"),108,(AppAction){.type=APP_RETRY},a->model.pending);
    }
}
static void settings(App *a)
{
    app_label(CLAY_STRING("Playback volume"),16,(Clay_Color){235,235,235,255});
    CLAY_AUTO_ID({.layout={.childGap=8}}) {
        (void)app_button(a,CLAY_ID("VolumeDown"),CLAY_STRING("-"),40,
            (AppAction){.type=APP_VOLUME,.value=a->model.volume.value - 0.1},!app_volume_available(a));
        (void)snprintf(a->volume,sizeof(a->volume),"%u%%",(unsigned)(a->model.volume.value * 100 + 0.5f));
        app_label(app_text(a,(Clay_String){false,(int32_t)strlen(a->volume),a->volume}),16,(Clay_Color){200,200,200,255});
        (void)app_button(a,CLAY_ID("VolumeUp"),CLAY_STRING("+"),40,
            (AppAction){.type=APP_VOLUME,.value=a->model.volume.value + 0.1},!app_volume_available(a));
    }
    (void)app_button(a,CLAY_ID("AppDetail"),a->model.detail ? CLAY_STRING("Hide details") : CLAY_STRING("Show details"),
        144,(AppAction){.type=APP_DETAIL},false);
}
Clay_RenderCommandArray app_layout(App *a, float width, float height, float dpi, UiWidgetInput input)
{
    if (!a || !a->alive || app_geometry(width,height,dpi,a->model.detail,&a->geometry) != APP_OK) {
        return (Clay_RenderCommandArray){0};
    }
    AppGeometry *g = &a->geometry;
    if (ui_widgets_begin(&a->widgets,g->width,g->height,input) != UI_WIDGET_OK) { return (Clay_RenderCommandArray){0}; }
    a->control_count = a->label_count = 0;
    a->messages.length=0; a->messages.text[0]=0; a->messages.full=false;
    CLAY(CLAY_ID("AppRoot"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_GROW()},
        .padding=CLAY_PADDING_ALL(8),.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},.backgroundColor={8,8,8,255}}) {
        CLAY(CLAY_ID("AppTitle"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(32)},.padding={28,0,0,0}}}) {
            app_label(a->services.demo ? CLAY_STRING("spoti-c | Offline demo") : CLAY_STRING("spoti-c"),18,(Clay_Color){225,225,225,255});
        }
        CLAY(CLAY_ID("AppBody"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_GROW()},.childGap=8}}) {
            app_sidebar(a);
            CLAY(CLAY_ID("AppMain"),{.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_GROW()},
                .padding=CLAY_PADDING_ALL(12),.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
                .backgroundColor={20,20,20,255},.cornerRadius=CLAY_CORNER_RADIUS(8),.clip={.horizontal=true,.vertical=true}}) {
                CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_GROW(),CLAY_SIZING_FIXED(28)},.childGap=8}}) {
                    (void)app_button(a,CLAY_ID("AppBack"),CLAY_STRING("Back"),56,(AppAction){.type=APP_BACK},!a->model.history_count);
                    CLAY_AUTO_ID({.layout={.sizing={CLAY_SIZING_FIXED(fmaxf(0,g->main-224)),CLAY_SIZING_FIXED(28)}},.clip={.horizontal=true,.vertical=true}}) {
                    app_label(a->model.location.screen == APP_LIBRARY ? CLAY_STRING("Your Library") :
                        a->model.location.screen == APP_SETTINGS ? CLAY_STRING("Settings") :
                        a->model.location.screen == APP_PLAYLIST ? CLAY_STRING("Playlist") :
                        a->model.location.screen == APP_SEARCH ? CLAY_STRING("Search") :
                        a->model.location.screen == APP_ALBUM ? CLAY_STRING("Album") :
                        a->model.location.screen == APP_ARTIST ? CLAY_STRING("Artist") :
                        a->model.location.screen == APP_LYRICS ? CLAY_STRING("Lyrics") :
                        a->model.location.screen == APP_SHELF ? CLAY_STRING("More for you") : CLAY_STRING("Home"),20,
                        (Clay_Color){245,245,245,255});
                    }
                    (void)app_button(a,CLAY_ID("AppReadMessages"),CLAY_STRING("Read messages"),128,
                        (AppAction){.type=APP_READ_MESSAGES},false);
                }
                if (a->model.notice[0]) {
                    app_message(a,(Clay_String){false,(int32_t)strnlen_s(a->model.notice,APP_NOTICE_BYTES),a->model.notice},a->model.notice_severity);
                } else if (a->model.last_result == APP_FULL || a->model.last_result == APP_FAILED || a->model.last_result == APP_UNAVAILABLE) {
                    app_message(a,CLAY_STRING("Action unavailable. Please try again."),APP_FAILURE);
                }
                if (a->cached && !a->model.logged_in && a->model.location.screen!=APP_SETTINGS) { startup_cached_view(a); }
                else if (a->model.location.screen == APP_SETTINGS) { settings(a); }
                else if (a->model.location.screen == APP_SEARCH) { app_search(a); }
                else if (a->model.location.screen == APP_ALBUM) { app_album(a); }
                else if (a->model.location.screen == APP_ARTIST) { app_artist(a); }
                else if (a->model.location.screen == APP_LYRICS) { app_lyrics(a); }
                else if (a->model.location.screen == APP_PLAYLIST && a->model.logged_in) { app_playlist(a); }
                else if (a->model.page && a->model.logged_in) {
                    if (a->model.status == APP_ERROR || !a->model.page->count) { status(a); }
                    if (a->model.page->count) {
                        if (a->model.location.screen == APP_HOME) { app_home(a); } else { app_library(a); }
                    }
                } else { status(a); }
            }
            if (g->detail) {
                CLAY(CLAY_ID("AppDetails"),{.layout={.sizing={CLAY_SIZING_FIXED(g->detail),CLAY_SIZING_GROW()},
                    .padding=CLAY_PADDING_ALL(12),.childGap=8,.layoutDirection=CLAY_TOP_TO_BOTTOM},
                    .backgroundColor={20,20,20,255},.clip={.horizontal=true,.vertical=true}}) {
                    app_label(CLAY_STRING("Selected"),18,(Clay_Color){210,210,210,255});
                    app_label((Clay_String){false,(int32_t)strlen(a->model.selected.title),a->model.selected.title},16,(Clay_Color){245,245,245,255});
                }
            }
        }
        app_now_playing(a);
    }
    a->commands = ui_widgets_end(&a->widgets); ++a->frames;
    bool found = false;
    for (uint32_t i = 0; i < a->control_count; ++i) { if (a->controls[i].id.id == a->widgets.focus) { found = true; } }
    if (!found && a->control_count) { a->widgets.focus = a->controls[0].id.id; }
    a->model.location.scroll = a->widgets.lists[0].scroll;
    return a->commands;
}
AppResult app_image(App *a, uint64_t gen, const AppEntity *entity, ArtworkId id,
    const ArtworkSource *sources, size_t count)
{
    if (!a || !entity || !a->alive || a->widgets.in_frame || !a->services.artwork) { return APP_INVALID; }
    if (gen != a->model.generation) { return APP_STALE; }
    uint32_t slot=0;
    while (slot<a->image_count && strcmp(a->images[slot].entity.uri,entity->uri)) { ++slot; }
    if (slot==APP_IMAGES) { return APP_FULL; }
    id.generation = gen;
    ArtResult r = artwork_request(a->services.artwork,id,sources,count);
    if (r != ART_OK && r != ART_PENDING) { return r == ART_FULL ? APP_FULL : APP_FAILED; }
    a->images[slot] = (AppImage){.tag=APP_CUSTOM_IMAGE,.entity=*entity,.id=id};
    if (slot==a->image_count) { ++a->image_count; } return APP_PENDING;
}
UiRenderResult app_render_custom(void *context, UiRenderFrame *frame, const Clay_RenderCommand *c)
{
    App *a = context;
    if (c->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
        if (!a->fonts) { return UI_RENDER_OK; }
        UiRenderResult r = font_render_clay(a->fonts,frame,c);
        /* More than one bounded preparation batch can be needed on a new
         * screen. The first queued miss already schedules the next dirty frame. */
        return r == UI_RENDER_FULL && font_stats(a->fonts->cache).pending ? UI_RENDER_OK : r;
    }
    if (c->commandType == CLAY_RENDER_COMMAND_TYPE_CUSTOM && c->renderData.custom.customData) {
        AppImage *image = c->renderData.custom.customData;
        UiRect box={c->boundingBox.x,c->boundingBox.y,c->boundingBox.width,c->boundingBox.height};
        if (image->tag==APP_CUSTOM_ICON) {
            AppGlyph *glyph=c->renderData.custom.customData;
            return ui_icon_draw(frame,box,glyph->icon,glyph->color);
        }
        if (image->tag==APP_CUSTOM_IMAGE && a->services.artwork) { return artwork_draw(a->services.artwork,image->id,frame,
            (UiRect){c->boundingBox.x,c->boundingBox.y,c->boundingBox.width,c->boundingBox.height}); }
    }
    return UI_RENDER_UNSUPPORTED;
}
