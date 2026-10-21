#ifndef SPOTIC_UI_RENDERER_H
#define SPOTIC_UI_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define UI_MAX_QUADS 4096u
#define UI_MAX_BATCHES 1024u
#define UI_MAX_CLIPS 32u
#define UI_MAX_TEXTURES 16u
#define UI_TEXTURE_BUDGET (16u * 1024u * 1024u)
#define UI_MAX_SURFACE 16384u

typedef enum UiRenderResult {
    UI_RENDER_OK, UI_RENDER_FULL, UI_RENDER_INVALID, UI_RENDER_UNSUPPORTED,
    UI_RENDER_DEVICE_LOST, UI_RENDER_OCCLUDED, UI_RENDER_SUSPENDED, UI_RENDER_OS_ERROR
} UiRenderResult;
typedef enum UiQuadMode { UI_QUAD_SOLID, UI_QUAD_BORDER, UI_QUAD_SHADOW,
    UI_QUAD_IMAGE, UI_QUAD_GLYPH, UI_QUAD_ICON } UiQuadMode;
typedef uint32_t UiTexture;
typedef struct UiRect { float x, y, w, h; } UiRect;
typedef struct QuadInstance {
    float box[4], color[4], uv[4], radii[4], border[4], effect[4];
} QuadInstance;
_Static_assert(sizeof(QuadInstance) == 96, "HLSL instance ABI");
_Static_assert(sizeof(QuadInstance) * UI_MAX_QUADS <= 512u * 1024u, "staging budget");
typedef struct UiBatch {
    uint32_t first, count;
    UiTexture texture;
    int32_t scissor[4];
} UiBatch;
typedef struct UiRenderFrame {
    QuadInstance quads[UI_MAX_QUADS];
    UiBatch batches[UI_MAX_BATCHES];
    UiRect clips[UI_MAX_CLIPS];
    uint32_t quad_count, batch_count, clip_count, width, height;
    float scale;
    UiRenderResult error;
} UiRenderFrame;

/* Caller-owned UI-thread storage; begin is a pointer/count reset, no allocation.
 * Positions/radii/widths are logical pixels, colors/UVs normalized floats.
 * A failed frame is sticky and cannot dispatch until the next begin. */
UiRenderResult ui_frame_begin(UiRenderFrame *f, uint32_t width, uint32_t height, float scale);
UiRenderResult ui_frame_clip_push(UiRenderFrame *f, UiRect clip, bool horizontal, bool vertical);
UiRenderResult ui_frame_clip_pop(UiRenderFrame *f);
UiRenderResult ui_frame_quad(UiRenderFrame *f, const QuadInstance *q, UiTexture texture);
typedef UiRenderResult (*UiBatchSink)(void *context, const UiRenderFrame *frame, const UiBatch *batch);
UiRenderResult ui_frame_dispatch(const UiRenderFrame *f, UiBatchSink sink, void *context);

/* GPU implementation uses C COM interfaces internally. All fields are opaque to
 * consumers; including this header does not initialize/load D3D for CPU tests. */
typedef struct UiRenderer UiRenderer;
typedef struct UiRendererConfig {
    void *hwnd; /* NULL creates an offscreen target. Borrowed native HWND. */
    uint32_t width, height, occlusion_message;
    bool warp, allow_warp_fallback, debug;
} UiRendererConfig;
typedef struct UiRendererStats {
    /* Logical buffer/texel sizes, not driver residency. System surfaces are a
     * two-buffer swapchain estimate; driver/shader implementation bytes unknown. */
    uint64_t owned_gpu_bytes, system_surface_bytes, draw_calls, presents, maps, texture_updates;
    uint32_t resources, textures, generation;
    bool warp, debug_layer;
} UiRendererStats;

typedef struct UiGpuTexture {
    struct ID3D11ShaderResourceView *view;
    uint32_t serial, bytes, channels, width, height;
} UiGpuTexture;
/* Caller-owned, zero-initialized, no copies while live. Fields are private to
 * the backend; forward COM declarations keep Windows headers out of batching. */
struct UiRenderer {
    UiRendererConfig config;
    UiRendererStats stats;
    struct ID3D11Device *device;
    struct ID3D11DeviceContext *context;
    struct IDXGIFactory2 *factory;
    struct IDXGISwapChain1 *swap;
    struct ID3D11Texture2D *target;
    struct ID3D11RenderTargetView *rtv;
    struct ID3D11Buffer *instances, *constants;
    struct ID3D11VertexShader *vs;
    struct ID3D11PixelShader *ps;
    struct ID3D11InputLayout *layout;
    struct ID3D11BlendState *blend;
    struct ID3D11RasterizerState *raster;
    struct ID3D11SamplerState *sampler;
    UiGpuTexture textures[UI_MAX_TEXTURES];
    uint32_t texture_bytes, serial, owner, occlusion_cookie;
    bool lost;
};
_Static_assert(sizeof(UiRenderer) <= 4096, "device state budget");
UiRenderResult ui_renderer_create(UiRenderer *r, const UiRendererConfig *config);
void ui_renderer_destroy(UiRenderer *r);
UiRenderResult ui_renderer_resize(UiRenderer *r, uint32_t width, uint32_t height);
UiRenderResult ui_renderer_recover(UiRenderer *r);
/* Allocations/resource creation only in create/resize/recover/texture/readback.
 * Submit never creates resources. DEVICE_LOST requires explicit recovery. */
UiRenderResult ui_renderer_submit(UiRenderer *r, const UiRenderFrame *f, const float clear[4]);
UiRenderResult ui_renderer_present(UiRenderer *r);
UiRenderResult ui_renderer_texture(UiRenderer *r, uint32_t width, uint32_t height,
    const unsigned char *rgba, size_t stride, UiTexture *out);
UiRenderResult ui_renderer_texture_release(UiRenderer *r, UiTexture texture);
UiRenderResult ui_renderer_texture_r8(UiRenderer *r, uint32_t width, uint32_t height,
    const unsigned char *coverage, size_t stride, UiTexture *out);
/* Safe preparation boundary only; R8 textures use DEFAULT usage for updates. */
UiRenderResult ui_renderer_texture_update_r8(UiRenderer *r, UiTexture texture,
    const unsigned char *coverage, size_t stride);
bool ui_renderer_texture_valid(const UiRenderer *r, UiTexture texture);
/* Offscreen testing only: caller buffer, RGBA8, no ownership transfer. */
UiRenderResult ui_renderer_readback(UiRenderer *r, unsigned char *rgba, size_t stride, size_t bytes);
UiRendererStats ui_renderer_stats(const UiRenderer *r);
/* Deliberate invalidation follows the same recovery path as removed devices. */
void ui_renderer_invalidate(UiRenderer *r);
/* Release owned resources first, report live objects when debug layers exist. */
bool ui_renderer_debug_report(UiRenderer *r);

#endif
