#ifndef SPOTIC_APP_DESKTOP_H
#define SPOTIC_APP_DESKTOP_H
#include "ui_surface.h"
#include "app_fixture.h"
#include "media_controls_winrt.h"
#include "startup.h"
typedef struct AppDesktop {
    UiSurface surface; App app; AppFixture *fixture; UiWidgetInput pointer;
    MediaControls media; MediaWinrt media_native; EventQueue *media_events;
    MediaResult media_result;
    Startup *startup;
    void (*startup_prepare)(UiWindow *);
} AppDesktop;
_Static_assert(sizeof(App)+sizeof(MediaControls)+sizeof(MediaWinrt)<=MEMORY_BUDGET_UI_STATE,"app + Windows media state budget");
MediaResult app_desktop_media_open(AppDesktop *, UiWindow *);
MediaResult app_desktop_media_bind(AppDesktop *, UiWindow *, MediaAdapter); /* Injected platform adapter. */
void app_desktop_media_prepare(AppDesktop *, uint64_t);
bool app_desktop_media_close(AppDesktop *);
void app_desktop_frame(void *, UiWindow *, uint32_t dirty);
void app_desktop_prepare(void *, UiWindow *, uint32_t dirty);
bool app_desktop_input(void *, UiWindow *, UINT, WPARAM, LPARAM, LRESULT *);
#endif
