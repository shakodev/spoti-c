#ifndef SPOTIC_APP_DESKTOP_H
#define SPOTIC_APP_DESKTOP_H
#include "ui_surface.h"
#include "app_fixture.h"
typedef struct AppDesktop { UiSurface surface; App app; AppFixture *fixture; UiWidgetInput pointer; } AppDesktop;
void app_desktop_frame(void *, UiWindow *, uint32_t dirty);
void app_desktop_prepare(void *, UiWindow *, uint32_t dirty);
bool app_desktop_input(void *, UiWindow *, UINT, WPARAM, LPARAM, LRESULT *);
#endif
