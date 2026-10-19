#include "renderer_internal.h"
#include "ui_vs.h"
#include "ui_ps.h"
#include <string.h>

static bool debug_messages_clean(ID3D11InfoQueue *queue)
{
    if (!queue) { return true; }
    UINT64 count = ID3D11InfoQueue_GetNumStoredMessagesAllowedByRetrievalFilter(queue);
    for (UINT64 i = 0; i < count; ++i) {
        union { D3D11_MESSAGE message; unsigned char storage[4096]; } entry;
        SIZE_T size = sizeof(entry);
        if (FAILED(ID3D11InfoQueue_GetMessage(queue,i,&entry.message,&size))) { return false; }
        D3D11_MESSAGE *m = &entry.message;
        if (m->Severity == D3D11_MESSAGE_SEVERITY_ERROR || m->Severity == D3D11_MESSAGE_SEVERITY_CORRUPTION ||
            (m->ID >= D3D11_MESSAGE_ID_LIVE_BUFFER && m->ID <= D3D11_MESSAGE_ID_LIVE_COUNTER)) {
            return false;
        }
    }
    return true;
}

bool ui_gpu_owner(const UiRenderer *r) { return r && r->owner == GetCurrentThreadId(); }

UiRenderResult ui_gpu_result(UiRenderer *r, HRESULT hr)
{
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) {
        r->lost = true; return UI_RENDER_DEVICE_LOST;
    }
    if (hr == DXGI_STATUS_OCCLUDED) { return UI_RENDER_OCCLUDED; }
    return SUCCEEDED(hr) ? UI_RENDER_OK : UI_RENDER_OS_ERROR;
}

void ui_gpu_account(UiRenderer *r)
{
    r->stats.owned_gpu_bytes = r->texture_bytes;
    if (r->instances) { r->stats.owned_gpu_bytes += sizeof(QuadInstance) * UI_MAX_QUADS; }
    if (r->constants) { r->stats.owned_gpu_bytes += 16; }
    uint64_t surface = (uint64_t)r->config.width * r->config.height * 4;
    if (r->target && !r->swap) { r->stats.owned_gpu_bytes += surface; }
    r->stats.system_surface_bytes = r->swap ? surface * 2 : 0;
    r->stats.generation = r->serial;
}

UiRenderResult ui_gpu_pipeline(UiRenderer *r)
{
    HRESULT hr = ID3D11Device_CreateVertexShader(r->device, ui_vs, sizeof(ui_vs), NULL, &r->vs);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    hr = ID3D11Device_CreatePixelShader(r->device, ui_ps, sizeof(ui_ps), NULL, &r->ps);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    D3D11_INPUT_ELEMENT_DESC input[6] = {0};
    const char *names[6] = {"BOX", "COLOR", "UV", "RADII", "BORDER", "EFFECT"};
    for (UINT i = 0; i < 6; ++i) {
        input[i].SemanticName = names[i]; input[i].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        input[i].AlignedByteOffset = i * 16; input[i].InputSlotClass = D3D11_INPUT_PER_INSTANCE_DATA;
        input[i].InstanceDataStepRate = 1;
    }
    hr = ID3D11Device_CreateInputLayout(r->device, input, 6, ui_vs, sizeof(ui_vs), &r->layout);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    D3D11_BUFFER_DESC buffer = {0};
    buffer.ByteWidth = sizeof(QuadInstance) * UI_MAX_QUADS; buffer.Usage = D3D11_USAGE_DYNAMIC;
    buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER; buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(r->device, &buffer, NULL, &r->instances);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    buffer.ByteWidth = 16; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = ID3D11Device_CreateBuffer(r->device, &buffer, NULL, &r->constants);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    D3D11_BLEND_DESC blend = {0};
    blend.RenderTarget[0] = (D3D11_RENDER_TARGET_BLEND_DESC){TRUE, D3D11_BLEND_ONE,
        D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE,
        D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL};
    hr = ID3D11Device_CreateBlendState(r->device, &blend, &r->blend);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    D3D11_RASTERIZER_DESC raster = {0};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE;
    raster.ScissorEnable = TRUE; raster.DepthClipEnable = TRUE;
    hr = ID3D11Device_CreateRasterizerState(r->device, &raster, &r->raster);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    D3D11_SAMPLER_DESC sampler = {0};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    hr = ID3D11Device_CreateSamplerState(r->device, &sampler, &r->sampler);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    ui_gpu_account(r); return UI_RENDER_OK;
}

UiRenderResult ui_gpu_target(UiRenderer *r)
{
    HRESULT hr;
    if (r->swap) {
        hr = IDXGISwapChain1_GetBuffer(r->swap, 0, &IID_ID3D11Texture2D, (void **)&r->target);
    } else {
        D3D11_TEXTURE2D_DESC target = {0};
        target.Width = r->config.width; target.Height = r->config.height;
        target.MipLevels = target.ArraySize = 1; target.SampleDesc.Count = 1;
        target.Format = DXGI_FORMAT_R8G8B8A8_UNORM; target.BindFlags = D3D11_BIND_RENDER_TARGET;
        hr = ID3D11Device_CreateTexture2D(r->device, &target, NULL, &r->target);
    }
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    hr = ID3D11Device_CreateRenderTargetView(r->device, (ID3D11Resource *)r->target, NULL, &r->rtv);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    ui_gpu_account(r); return UI_RENDER_OK;
}

static HRESULT create_device(UiRenderer *r, D3D_DRIVER_TYPE driver, bool debug)
{
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | (debug ? D3D11_CREATE_DEVICE_DEBUG : 0);
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(NULL, driver, NULL, flags, &level, 1,
        D3D11_SDK_VERSION, &r->device, NULL, &r->context);
    if (SUCCEEDED(hr)) {
        UI_OWN(r); UI_OWN(r); r->stats.warp = driver == D3D_DRIVER_TYPE_WARP;
        r->stats.debug_layer = debug;
    }
    return hr;
}

UiRenderResult ui_renderer_create(UiRenderer *r, const UiRendererConfig *config)
{
    if (!r || !config || r->device || !config->width || !config->height ||
        config->width > UI_MAX_SURFACE || config->height > UI_MAX_SURFACE) { return UI_RENDER_INVALID; }
    r->owner = GetCurrentThreadId(); r->config = *config; r->lost = false;
    D3D_DRIVER_TYPE driver = config->warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    HRESULT hr = create_device(r, driver, config->debug);
    if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING) { hr = create_device(r, driver, false); }
    if (FAILED(hr) && config->allow_warp_fallback && !config->warp) {
        hr = create_device(r, D3D_DRIVER_TYPE_WARP, false);
    }
    if (FAILED(hr)) {
        return hr == DXGI_ERROR_UNSUPPORTED || hr == E_NOINTERFACE ? UI_RENDER_UNSUPPORTED : ui_gpu_result(r, hr);
    }
    UiRenderResult result = ui_gpu_pipeline(r);
    if (result == UI_RENDER_OK && config->hwnd) {
        IDXGIDevice *dxgi = NULL; IDXGIAdapter *adapter = NULL;
        hr = ID3D11Device_QueryInterface(r->device, &IID_IDXGIDevice, (void **)&dxgi);
        if (SUCCEEDED(hr)) { hr = IDXGIDevice_GetAdapter(dxgi, &adapter); }
        if (SUCCEEDED(hr)) { hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&r->factory); }
        if (adapter) { IDXGIAdapter_Release(adapter); } if (dxgi) { IDXGIDevice_Release(dxgi); }
        if (SUCCEEDED(hr)) { UI_OWN(r); }
        if (SUCCEEDED(hr)) {
            DXGI_SWAP_CHAIN_DESC1 desc = {0};
            desc.Width = config->width; desc.Height = config->height;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            hr = IDXGIFactory2_CreateSwapChainForHwnd(r->factory, (IUnknown *)r->device,
                config->hwnd, &desc, NULL, NULL, &r->swap);
            if (SUCCEEDED(hr)) { UI_OWN(r); }
        }
        if (SUCCEEDED(hr)) {
            (void)IDXGIFactory2_MakeWindowAssociation(r->factory, config->hwnd, DXGI_MWA_NO_ALT_ENTER);
            if (config->occlusion_message) {
                DWORD cookie = 0;
                hr = IDXGIFactory2_RegisterOcclusionStatusWindow(r->factory, config->hwnd,
                    config->occlusion_message, &cookie);
                if (SUCCEEDED(hr)) { r->occlusion_cookie = (uint32_t)cookie; }
            }
        }
        result = ui_gpu_result(r, hr);
    }
    if (result == UI_RENDER_OK) { result = ui_gpu_target(r); }
    if (result != UI_RENDER_OK) { ui_renderer_destroy(r); }
    return result;
}

void ui_renderer_destroy(UiRenderer *r)
{
    if (!ui_gpu_owner(r)) { return; }
    if (r->context) { ID3D11DeviceContext_ClearState(r->context); ID3D11DeviceContext_Flush(r->context); }
    if (r->occlusion_cookie && r->factory) {
        IDXGIFactory2_UnregisterOcclusionStatus(r->factory, r->occlusion_cookie); r->occlusion_cookie = 0;
    }
    for (unsigned i = 0; i < UI_MAX_TEXTURES; ++i) { UI_RELEASE(r, textures[i].view); }
    r->texture_bytes = r->stats.textures = 0;
    UI_RELEASE(r, rtv); UI_RELEASE(r, target); UI_RELEASE(r, swap); UI_RELEASE(r, factory);
    if (r->context) { ID3D11DeviceContext_Flush(r->context); }
    UI_RELEASE(r, sampler); UI_RELEASE(r, raster); UI_RELEASE(r, blend); UI_RELEASE(r, layout);
    UI_RELEASE(r, vs); UI_RELEASE(r, ps); UI_RELEASE(r, instances); UI_RELEASE(r, constants);
    UI_RELEASE(r, context); UI_RELEASE(r, device);
    ui_gpu_account(r);
}

UiRenderResult ui_renderer_resize(UiRenderer *r, uint32_t width, uint32_t height)
{
    if (!ui_gpu_owner(r) || !r->device || width > UI_MAX_SURFACE || height > UI_MAX_SURFACE) { return UI_RENDER_INVALID; }
    if (r->lost) { return UI_RENDER_DEVICE_LOST; }
    if (!width || !height) { return UI_RENDER_SUSPENDED; }
    if (width == r->config.width && height == r->config.height && r->rtv) { return UI_RENDER_OK; }
    ID3D11DeviceContext_OMSetRenderTargets(r->context, 0, NULL, NULL);
    UI_RELEASE(r, rtv); UI_RELEASE(r, target);
    HRESULT hr = r->swap ? IDXGISwapChain1_ResizeBuffers(r->swap, 0, width, height, DXGI_FORMAT_UNKNOWN, 0) : S_OK;
    if (FAILED(hr)) { ui_gpu_account(r); return ui_gpu_result(r, hr); }
    r->config.width = width; r->config.height = height;
    UiRenderResult result = ui_gpu_target(r);
    if (result != UI_RENDER_OK) { UI_RELEASE(r, rtv); UI_RELEASE(r, target); ui_gpu_account(r); }
    return result;
}

void ui_renderer_invalidate(UiRenderer *r) { if (ui_gpu_owner(r)) { r->lost = true; } }
UiRenderResult ui_renderer_recover(UiRenderer *r)
{
    if (!ui_gpu_owner(r) || !r->lost) { return UI_RENDER_INVALID; }
    UiRendererConfig config = r->config;
    ui_renderer_destroy(r); return ui_renderer_create(r, &config);
}

UiRendererStats ui_renderer_stats(const UiRenderer *r) { return r ? r->stats : (UiRendererStats){0}; }

bool ui_renderer_debug_report(UiRenderer *r)
{
    if (!ui_gpu_owner(r)) { return false; }
    ID3D11Debug *debug = NULL;
    ID3D11InfoQueue *queue = NULL;
    if (r->device) { (void)ID3D11Device_QueryInterface(r->device, &IID_ID3D11Debug, (void **)&debug); }
    if (r->device) { (void)ID3D11Device_QueryInterface(r->device, &IID_ID3D11InfoQueue, (void **)&queue); }
    ui_renderer_destroy(r);
    bool clean = r->stats.resources == 0 && r->stats.owned_gpu_bytes == 0;
    if (debug) {
        clean = SUCCEEDED(ID3D11Debug_ReportLiveDeviceObjects(debug,
            D3D11_RLDO_SUMMARY | D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL)) && clean;
        ID3D11Debug_Release(debug);
    }
    clean = debug_messages_clean(queue) && clean;
    if (queue) { ID3D11InfoQueue_Release(queue); }
    /* DXGI debug is optional and dynamically resolved, never a runtime import. */
    HMODULE module = LoadLibraryW(L"dxgidebug.dll");
    if (module) {
        typedef HRESULT (WINAPI *GetDebug)(UINT, REFIID, void **);
        FARPROC proc = GetProcAddress(module, "DXGIGetDebugInterface1"); GetDebug get = NULL;
        memcpy(&get, &proc, sizeof(get));
        IDXGIDebug *dxgi = NULL;
        if (get && SUCCEEDED(get(0, &IID_IDXGIDebug, (void **)&dxgi))) {
            clean = SUCCEEDED(IDXGIDebug_ReportLiveObjects(dxgi, DXGI_DEBUG_ALL,
                DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_IGNORE_INTERNAL)) && clean;
            IDXGIDebug_Release(dxgi);
        }
        FreeLibrary(module);
    }
    return clean;
}
