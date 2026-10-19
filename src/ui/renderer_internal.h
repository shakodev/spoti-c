#ifndef SPOTIC_UI_RENDERER_INTERNAL_H
#define SPOTIC_UI_RENDERER_INTERNAL_H
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "renderer.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3d11sdklayers.h>
#include <dxgidebug.h>

#define UI_OWN(r) (++(r)->stats.resources)
#define UI_RELEASE(r, field) do { if ((r)->field) { \
    IUnknown_Release((IUnknown *)(r)->field); (r)->field = NULL; --(r)->stats.resources; \
} } while (0)
UiRenderResult ui_gpu_result(UiRenderer *r, HRESULT hr);
void ui_gpu_account(UiRenderer *r);
UiRenderResult ui_gpu_pipeline(UiRenderer *r);
UiRenderResult ui_gpu_target(UiRenderer *r);
bool ui_gpu_owner(const UiRenderer *r);
ID3D11ShaderResourceView *ui_gpu_texture_view(UiRenderer *r, UiTexture handle);
#endif
