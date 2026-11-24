#include "app.h"
#include <math.h>
#include <string.h>

void app_control_hint(App *a)
{
    if (a->menu.open || a->widgets.active) { return; }
    for (uint32_t n=0;n<a->control_count;++n) {
        const AppControl *control=&a->controls[n];
        if (control->id.id!=a->widgets.hover) { continue; }
        const char *label=NULL;
        if (control->id.id==CLAY_ID("CatalogPlay").id) {
            label=control->disabled ? "No playable track is available on this page" : "Play first track";
        } else if (control->id.id==CLAY_ID("PlaylistPlay").id) {
            label=control->disabled ? "No playable track is available in this selection" : "Play playlist";
        }
        if (!label) { return; }
        Clay_ElementData bounds=Clay_GetElementData(control->id);
        float x=fmaxf(8,fminf(bounds.boundingBox.x,a->geometry.width-272));
        float y=fmaxf(8,fminf(bounds.boundingBox.y+bounds.boundingBox.height+8,a->geometry.height-a->geometry.bar-64));
        CLAY(CLAY_ID("ControlHint"),{.layout={.sizing={CLAY_SIZING_FIXED(256),CLAY_SIZING_FIT()},.padding=CLAY_PADDING_ALL(8)},
            .backgroundColor=UI_ELEVATED_HOVER,.cornerRadius=CLAY_CORNER_RADIUS(4),
            .floating={.attachTo=CLAY_ATTACH_TO_ROOT,.offset={x,y},.zIndex=90,
                .pointerCaptureMode=CLAY_POINTER_CAPTURE_MODE_PASSTHROUGH}}) {
            CLAY_TEXT(((Clay_String){true,(int32_t)strlen(label),label}),
                CLAY_TEXT_CONFIG({.fontSize=13,.textColor=UI_TEXT,.wrapMode=CLAY_TEXT_WRAP_WORDS}));
        }
        return;
    }
}
