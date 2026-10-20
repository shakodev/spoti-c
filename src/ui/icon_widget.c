#include "icons_internal.h"
#include <math.h>

void ui_icon(UiWidgets *ui, Clay_ElementId id, UiIcon icon, float size,
    Clay_Color color, bool rtl)
{
    if (!ui) { return; }
    if (!ui->in_frame) { ui_widgets_fail(ui,UI_WIDGET_INVALID); return; }
    if (ui->error != UI_WIDGET_OK) { return; }
    if (!id.id || (unsigned)icon >= UI_ICON_COUNT || !isfinite(size) ||
        size <= 0 || size > 1000000 || !isfinite(color.r) || !isfinite(color.g) ||
        !isfinite(color.b) || !isfinite(color.a) || color.r < 0 || color.r > 255 ||
        color.g < 0 || color.g > 255 || color.b < 0 || color.b > 255 || color.a < 0 || color.a > 255) {
        ui_widgets_fail(ui,UI_WIDGET_INVALID); return;
    }
    if (++ui->declarations > 400) { ui_widgets_fail(ui,UI_WIDGET_FULL); return; }
    UiIconCommand *descriptor = ui_widgets_scratch(ui,sizeof(*descriptor),_Alignof(UiIconCommand));
    if (!descriptor) { return; }
    *descriptor = (UiIconCommand){UI_ICON_COMMAND_MAGIC,icon,color,rtl};
    CLAY(id,{.layout={.sizing={CLAY_SIZING_FIXED(size),CLAY_SIZING_FIXED(size)}},
        .custom={.customData=descriptor},.userData=(void *)&ui_icon_command_tag}) { }
}
