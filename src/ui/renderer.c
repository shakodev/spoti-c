#include "renderer_internal.h"
#include <math.h>
#include <string.h>

ID3D11ShaderResourceView *ui_gpu_texture_view(UiRenderer *r, UiTexture handle)
{
    uint32_t slot = handle & 255u, serial = handle >> 8;
    if (!slot || slot > UI_MAX_TEXTURES || !serial) { return NULL; }
    UiGpuTexture *t = &r->textures[slot - 1];
    return t->serial == serial ? t->view : NULL;
}
bool ui_renderer_texture_valid(const UiRenderer *r, UiTexture texture)
{
    if (!ui_gpu_owner(r) || r->lost) { return false; }
    uint32_t slot = texture & 255u;
    return slot && slot <= UI_MAX_TEXTURES && r->textures[slot - 1].view &&
        r->textures[slot - 1].serial == (texture >> 8);
}

static UiRenderResult texture_create(UiRenderer *r, uint32_t width, uint32_t height,
    const unsigned char *rgba, size_t stride, UiTexture *out, uint32_t channels)
{
    if (out) { *out = 0; }
    if (!ui_gpu_owner(r) || !r->device || !out || !rgba || !width || !height ||
        width > UI_MAX_SURFACE || height > UI_MAX_SURFACE || stride < (size_t)width * channels ||
        stride > UINT32_MAX || stride > SIZE_MAX / height) { return UI_RENDER_INVALID; }
    if (r->lost) { return UI_RENDER_DEVICE_LOST; }
    uint64_t bytes = (uint64_t)width * height * channels;
    if (bytes > UI_TEXTURE_BUDGET - r->texture_bytes || r->serial == 0xffffffu) { return UI_RENDER_FULL; }
    unsigned slot = 0;
    while (slot < UI_MAX_TEXTURES && r->textures[slot].view) { ++slot; }
    if (slot == UI_MAX_TEXTURES) { return UI_RENDER_FULL; }
    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = channels == 1 ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = channels == 1 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = {rgba, (UINT)stride, 0};
    ID3D11Texture2D *texture = NULL;
    HRESULT hr = ID3D11Device_CreateTexture2D(r->device, &desc, &data, &texture);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); }
    hr = ID3D11Device_CreateShaderResourceView(r->device, (ID3D11Resource *)texture,
        NULL, &r->textures[slot].view);
    ID3D11Texture2D_Release(texture);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); } UI_OWN(r);
    UiGpuTexture *t = &r->textures[slot]; t->bytes = (uint32_t)bytes; t->serial = ++r->serial;
    t->channels = channels; t->width = width; t->height = height;
    r->texture_bytes += t->bytes; ++r->stats.textures;
    *out = (t->serial << 8) | (slot + 1); ui_gpu_account(r);
    return UI_RENDER_OK;
}

UiRenderResult ui_renderer_texture(UiRenderer *r, uint32_t width, uint32_t height,
    const unsigned char *rgba, size_t stride, UiTexture *out)
{
    return texture_create(r,width,height,rgba,stride,out,4);
}
UiRenderResult ui_renderer_texture_r8(UiRenderer *r, uint32_t width, uint32_t height,
    const unsigned char *coverage, size_t stride, UiTexture *out)
{
    return texture_create(r,width,height,coverage,stride,out,1);
}
UiRenderResult ui_renderer_texture_update_r8(UiRenderer *r, UiTexture texture,
    const unsigned char *coverage, size_t stride)
{
    if (!ui_gpu_owner(r) || !coverage || !ui_gpu_texture_view(r,texture)) { return UI_RENDER_INVALID; }
    if (r->lost) { return UI_RENDER_DEVICE_LOST; }
    UiGpuTexture *t = &r->textures[(texture & 255u) - 1];
    if (t->channels != 1 || stride < t->width || stride > UINT32_MAX || stride > SIZE_MAX / t->height) { return UI_RENDER_INVALID; }
    ID3D11Resource *resource = NULL;
    ID3D11ShaderResourceView_GetResource(t->view,&resource);
    ID3D11DeviceContext_UpdateSubresource(r->context,resource,0,NULL,coverage,(UINT)stride,0);
    ++r->stats.texture_updates;
    ID3D11Resource_Release(resource);
    return ui_gpu_result(r,ID3D11Device_GetDeviceRemovedReason(r->device));
}

UiRenderResult ui_renderer_texture_release(UiRenderer *r, UiTexture texture)
{
    if (!ui_gpu_owner(r) || !ui_gpu_texture_view(r, texture)) { return UI_RENDER_INVALID; }
    unsigned i = (texture & 255u) - 1;
    /* Unbind before dropping our SRV reference; avoids deferred texture lifetime. */
    ID3D11ShaderResourceView *none = NULL;
    ID3D11DeviceContext_PSSetShaderResources(r->context, 0, 1, &none);
    r->texture_bytes -= r->textures[i].bytes; --r->stats.textures;
    UI_RELEASE(r, textures[i].view); ui_gpu_account(r);
    return UI_RENDER_OK;
}

static UiRenderResult validate_batch(void *context, const UiRenderFrame *f, const UiBatch *b)
{
    UiRenderer *r = context;
    if ((b->texture && !ui_gpu_texture_view(r, b->texture)) ||
        b->scissor[0] < 0 || b->scissor[1] < 0 || b->scissor[2] > (int32_t)f->width ||
        b->scissor[3] > (int32_t)f->height || b->scissor[2] < b->scissor[0] ||
        b->scissor[3] < b->scissor[1]) { return UI_RENDER_INVALID; }
    return UI_RENDER_OK;
}

static UiRenderResult draw_batch(void *context, const UiRenderFrame *f, const UiBatch *b)
{
    (void)f; UiRenderer *r = context;
    D3D11_RECT scissor = {b->scissor[0], b->scissor[1], b->scissor[2], b->scissor[3]};
    ID3D11ShaderResourceView *texture = b->texture ? ui_gpu_texture_view(r, b->texture) : NULL;
    ID3D11DeviceContext_RSSetScissorRects(r->context, 1, &scissor);
    ID3D11DeviceContext_PSSetShaderResources(r->context, 0, 1, &texture);
    ID3D11DeviceContext_DrawInstanced(r->context, 6, b->count, 0, b->first);
    ++r->stats.draw_calls; return UI_RENDER_OK;
}

UiRenderResult ui_renderer_submit(UiRenderer *r, const UiRenderFrame *f, const float clear[4])
{
    if (!ui_gpu_owner(r) || !r->device || !r->rtv || !f || !clear ||
        f->width != r->config.width || f->height != r->config.height ||
        !isfinite(f->scale) || f->scale < 0.25f || f->scale > 8) { return UI_RENDER_INVALID; }
    if (r->lost) { return UI_RENDER_DEVICE_LOST; }
    for (unsigned i = 0; i < 4; ++i) {
        if (!isfinite(clear[i]) || clear[i] < 0 || clear[i] > 1) { return UI_RENDER_INVALID; }
    }
    UiRenderResult valid = ui_frame_dispatch(f, validate_batch, r);
    if (valid != UI_RENDER_OK) { return valid; }
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ID3D11DeviceContext_Map(r->context, (ID3D11Resource *)r->constants,
        0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); }
    float view[4] = {(float)f->width, (float)f->height, f->scale, 0};
    memcpy(mapped.pData, view, sizeof(view));
    ID3D11DeviceContext_Unmap(r->context, (ID3D11Resource *)r->constants, 0); ++r->stats.maps;
    if (f->quad_count) {
        hr = ID3D11DeviceContext_Map(r->context, (ID3D11Resource *)r->instances,
            0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { return ui_gpu_result(r, hr); }
        memcpy(mapped.pData, f->quads, sizeof(QuadInstance) * f->quad_count);
        ID3D11DeviceContext_Unmap(r->context, (ID3D11Resource *)r->instances, 0); ++r->stats.maps;
    }
    D3D11_VIEWPORT viewport = {0, 0, (float)f->width, (float)f->height, 0, 1};
    UINT stride = sizeof(QuadInstance), offset = 0;
    ID3D11DeviceContext_IASetInputLayout(r->context, r->layout);
    ID3D11DeviceContext_IASetVertexBuffers(r->context, 0, 1, &r->instances, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(r->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(r->context, r->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(r->context, r->ps, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(r->context, 0, 1, &r->constants);
    ID3D11DeviceContext_PSSetConstantBuffers(r->context, 0, 1, &r->constants);
    ID3D11DeviceContext_PSSetSamplers(r->context, 0, 1, &r->sampler);
    ID3D11DeviceContext_RSSetViewports(r->context, 1, &viewport);
    ID3D11DeviceContext_RSSetState(r->context, r->raster);
    ID3D11DeviceContext_OMSetBlendState(r->context, r->blend, NULL, UINT32_MAX);
    ID3D11DeviceContext_OMSetRenderTargets(r->context, 1, &r->rtv, NULL);
    float premult[4] = {clear[0] * clear[3], clear[1] * clear[3], clear[2] * clear[3], clear[3]};
    ID3D11DeviceContext_ClearRenderTargetView(r->context, r->rtv, premult);
    return ui_frame_dispatch(f, draw_batch, r);
}

UiRenderResult ui_renderer_present(UiRenderer *r)
{
    if (!ui_gpu_owner(r) || !r->swap) { return UI_RENDER_INVALID; }
    if (r->lost) { return UI_RENDER_DEVICE_LOST; }
    ++r->stats.presents;
    return ui_gpu_result(r, IDXGISwapChain1_Present(r->swap, 1, 0));
}

UiRenderResult ui_renderer_readback(UiRenderer *r, unsigned char *rgba, size_t stride, size_t bytes)
{
    if (!ui_gpu_owner(r) || !r->target || r->swap || !rgba ||
        stride < (size_t)r->config.width * 4 || stride > SIZE_MAX / r->config.height ||
        bytes < stride * r->config.height) { return UI_RENDER_INVALID; }
    if (r->lost) { return UI_RENDER_DEVICE_LOST; }
    D3D11_TEXTURE2D_DESC desc; ID3D11Texture2D_GetDesc(r->target, &desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.BindFlags = 0;
    ID3D11Texture2D *staging = NULL;
    HRESULT hr = ID3D11Device_CreateTexture2D(r->device, &desc, NULL, &staging);
    if (FAILED(hr)) { return ui_gpu_result(r, hr); }
    ID3D11DeviceContext_CopyResource(r->context, (ID3D11Resource *)staging, (ID3D11Resource *)r->target);
    D3D11_MAPPED_SUBRESOURCE map;
    hr = ID3D11DeviceContext_Map(r->context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (SUCCEEDED(hr)) {
        for (uint32_t row = 0; row < desc.Height; ++row) {
            memcpy(rgba + row * stride, (unsigned char *)map.pData + row * (size_t)map.RowPitch, (size_t)desc.Width * 4);
        }
        ID3D11DeviceContext_Unmap(r->context, (ID3D11Resource *)staging, 0);
    }
    ID3D11Texture2D_Release(staging);
    return ui_gpu_result(r, hr);
}
