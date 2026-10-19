#include "ui_surface.h"

static const char caption[] = "spoti-c";
static void text_wake(void *context) { ui_scheduler_dirty(&((UiWindow *)context)->scheduler,UI_DIRTY_STATE); }

UiRenderResult ui_surface_create(UiSurface *s, UiWindow *w, bool warp)
{
    if (!s || !w || !w->hwnd || s->ready) { return UI_RENDER_INVALID; }
    RECT rect; GetClientRect(w->hwnd, &rect);
    UiRendererConfig config = {.hwnd = w->hwnd, .width = (uint32_t)(rect.right - rect.left),
        .height = (uint32_t)(rect.bottom - rect.top), .warp = warp, .allow_warp_fallback = true,
        .occlusion_message = UI_WINDOW_OCCLUSION_MESSAGE};
    s->last_result = ui_renderer_create(&s->renderer, &config);
    s->ready = s->last_result == UI_RENDER_OK;
    if (s->ready) {
        s->text = (FontMeasureContext){.cache=&s->fonts,.config={.size=20,.dpi=(uint16_t)w->dpi,.weight=700}};
        s->text.last_result = font_cache_create(&s->fonts,text_wake,w);
        s->fonts_ready = s->text.last_result == FONT_OK;
        if (!s->fonts_ready) { ui_surface_destroy(s); s->last_result = UI_RENDER_OS_ERROR; }
    }
    return s->last_result;
}

void ui_surface_destroy(UiSurface *s)
{
    if (s->fonts_ready) { font_cache_destroy(&s->fonts); s->fonts_ready = false; }
    ui_renderer_destroy(&s->renderer); s->ready = false;
}

void ui_surface_prepare(void *context, UiWindow *w, uint32_t dirty)
{
    (void)dirty; UiSurface *s = context;
    if (s->fallback_pending) {
        ui_surface_destroy(s); s->fallback_pending = false;
    }
    if (!s->ready) { return; }
    if (s->recovery_pending) {
        s->recovery_pending = false;
        if (s->recovery_attempted) { ui_surface_destroy(s); return; }
        s->recovery_attempted = true;
        s->last_result = ui_renderer_recover(&s->renderer);
        if (s->last_result != UI_RENDER_OK) { ui_surface_destroy(s); return; }
    }
    RECT rect; GetClientRect(w->hwnd, &rect);
    s->last_result = ui_renderer_resize(&s->renderer, (uint32_t)(rect.right - rect.left),
        (uint32_t)(rect.bottom - rect.top));
    if (s->last_result == UI_RENDER_DEVICE_LOST) {
        s->recovery_pending = true; ui_scheduler_dirty(&w->scheduler, UI_DIRTY_DEVICE);
    } else if (s->last_result != UI_RENDER_OK && s->last_result != UI_RENDER_SUSPENDED) {
        ui_surface_destroy(s);
    }
    if (s->ready && s->fonts_ready && s->last_result == UI_RENDER_OK) {
        if (s->text.config.dpi != w->dpi) {
            (void)font_cache_reset(&s->fonts); s->text.config.dpi = (uint16_t)w->dpi;
        }
        s->text.last_result = font_prepare_pending(&s->fonts);
        if (s->text.last_result == FONT_OK) {
            s->text.last_result = font_prepare_text(&s->fonts,(Clay_StringSlice){sizeof(caption)-1,caption,caption},
                &s->text.config,&s->caption);
        }
        if (s->text.last_result == FONT_OK) { s->text.last_result = font_upload_renderer(&s->fonts,&s->renderer); }
    }
}

static UiRenderResult panel(UiRenderFrame *f, float x, float y, float width, float height, float value)
{
    QuadInstance q = {.box = {x,y,width,height}, .color = {value,value,value,1}, .radii = {8,8,8,8}};
    return ui_frame_quad(f, &q, 0);
}

void ui_surface_frame(void *context, UiWindow *w, uint32_t dirty)
{
    (void)dirty; UiSurface *s = context;
    if (!s->ready) {
        /* Swapchain has been released before using GDI on the fallback path. */
        RECT rect; GetClientRect(w->hwnd, &rect); HDC dc = GetDC(w->hwnd);
        if (dc) {
            FillRect(dc, &rect, GetStockObject(BLACK_BRUSH));
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(240,240,240));
            rect.left += MulDiv(40, (int)w->dpi, 96);
            DrawTextW(dc, L"spoti-c (DX11 unavailable)", -1, &rect, DT_LEFT | DT_SINGLELINE);
            ReleaseDC(w->hwnd, dc);
        }
        return;
    }
    if (s->last_result == UI_RENDER_SUSPENDED || s->recovery_pending) { return; }
    RECT rect; GetClientRect(w->hwnd, &rect);
    float scale = (float)w->dpi / 96, width = (float)rect.right / scale, height = (float)rect.bottom / scale;
    s->last_result = ui_frame_begin(&s->frame, (uint32_t)rect.right, (uint32_t)rect.bottom, scale);
    /* Foundation surface until the Clay screens/font atlas land. */
    if (s->last_result == UI_RENDER_OK) { s->last_result = panel(&s->frame,8,52,180,height - 124,0.065f); }
    if (s->last_result == UI_RENDER_OK) { s->last_result = panel(&s->frame,196,52,width - 204,height - 124,0.09f); }
    if (s->last_result == UI_RENDER_OK) { s->last_result = panel(&s->frame,8,height - 64,width - 16,56,0.065f); }
    if (s->fonts_ready && s->text.last_result == FONT_OK) {
        const float white[4] = {0.95f,0.95f,0.95f,1};
        s->text.last_result = font_render_text(&s->fonts,s->caption,&s->frame,40,4,white);
    }
    const float clear[4] = {0,0,0,1};
    if (s->last_result == UI_RENDER_OK) { s->last_result = ui_renderer_submit(&s->renderer, &s->frame, clear); }
    if (s->last_result == UI_RENDER_OK) { s->last_result = ui_renderer_present(&s->renderer); }
    if (s->last_result == UI_RENDER_OCCLUDED) { ui_window_set_occluded(w, true); }
    else if (s->last_result == UI_RENDER_DEVICE_LOST) {
        s->recovery_pending = true; ui_scheduler_dirty(&w->scheduler, UI_DIRTY_DEVICE);
    } else if (s->last_result != UI_RENDER_OK) {
        s->fallback_pending = true; ui_scheduler_dirty(&w->scheduler, UI_DIRTY_DEVICE);
    }
}
