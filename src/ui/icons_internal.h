#ifndef SPOTIC_UI_ICONS_INTERNAL_H
#define SPOTIC_UI_ICONS_INTERNAL_H
#include "icons.h"

#define UI_ICON_COMMAND_MAGIC UINT32_C(0x5549434e)
typedef struct UiIconCommand {
    uint32_t magic;
    UiIcon icon;
    Clay_Color color;
    bool rtl;
} UiIconCommand;
/* An address-identity tag prevents inspecting unrelated customData. */
extern const uint32_t ui_icon_command_tag;
UiRenderResult ui_icon_dispatch(UiRenderFrame *frame,
    const Clay_RenderCommand *command, bool *handled);
#endif
