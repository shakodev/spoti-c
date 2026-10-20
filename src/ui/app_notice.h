#ifndef SPOTIC_APP_NOTICE_H
#define SPOTIC_APP_NOTICE_H
#include "font.h"
#include "app_result.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define APP_NOTICE_BYTES 1024u
#define APP_MESSAGE_BYTES 4096u
typedef enum AppSeverity { APP_INFORMATION, APP_WARNING, APP_FAILURE } AppSeverity;
typedef struct AppMessages {
    char text[APP_MESSAGE_BYTES], details[APP_MESSAGE_BYTES];
    uint32_t length;
    bool full, requested;
} AppMessages;
struct App;
/* UI preparation only; input is copied or replaced by an explicit diagnostic. */
AppResult app_set_notice(struct App *, AppSeverity, Clay_String);
/* Layout only. Copies full text into the bounded frame transcript. */
void app_message(struct App *, Clay_String, AppSeverity);
/* Input/preparation only. Snapshot before entering the modal message loop. */
AppResult app_messages_request(struct App *);
bool app_messages_dialog(HWND owner, const char *utf8);
#endif
