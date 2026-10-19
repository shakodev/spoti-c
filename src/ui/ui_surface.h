#ifndef SPOTIC_UI_SURFACE_H
#define SPOTIC_UI_SURFACE_H
#include "renderer.h"
#include "window.h"
#include "font.h"
struct App;
typedef struct UiSurface {
    UiRenderer renderer;
    UiRenderFrame frame;
    FontCache fonts;
    FontMeasureContext text;
    FontHandle caption;
    struct App *app; /* Optional borrowed shell; buffers outlive font unbinding. */
    bool fonts_ready;
    bool ready, recovery_pending, fallback_pending, recovery_attempted;
    UiRenderResult last_result;
} UiSurface;
/* Setup outside the message loop; fall back explicitly if DX11 is unavailable. */
UiRenderResult ui_surface_create(UiSurface *surface, UiWindow *window, bool warp);
void ui_surface_destroy(UiSurface *surface);
void ui_surface_prepare(void *context, UiWindow *window, uint32_t dirty);
void ui_surface_frame(void *context, UiWindow *window, uint32_t dirty);
#endif
