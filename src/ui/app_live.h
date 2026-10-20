#ifndef SPOTIC_APP_LIVE_H
#define SPOTIC_APP_LIVE_H
#include "app.h"
#include "core/worker.h"
typedef struct AppLive AppLive;
AppResult app_live_open(Arena *, EventQueue *, App *, const wchar_t *directory, AppLive **);
AppServices app_live_services(AppLive *);
void app_live_pump(AppLive *);
bool app_live_close(AppLive *); /* App disposed, readers cancelled, before core/event teardown. */
#endif
