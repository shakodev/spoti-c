#ifndef APP_SHELL_TEST_SUPPORT_H
#define APP_SHELL_TEST_SUPPORT_H
#include "ui/app.h"
#include "ui/app_fixture.h"
#include "support/test_check.h"
#include <string.h>

extern App app;
extern CollectionService *collection;
extern JsonPagePool *pages;
extern unsigned requests, cancellations, authentications;
extern AppRequest latest;
void shell_open(Player *player);
void shell_close(void);
void shell_action(AppActionType type);
const CollectionPage *shell_page(AppRequest, const char *name);
void shell_deliver(const char *name);
Clay_RenderCommandArray shell_layout(float width, float height, float dpi);
int shell_visual(void);
void shell_native(void);
void shell_font_frames(void);
void shell_account(void);
void shell_artwork(void);
void shell_volume(void);
void shell_now_playing(void);
#endif
