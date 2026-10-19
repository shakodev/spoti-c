#include "font_internal.h"

static HRESULT STDMETHODCALLTYPE query(DwRenderer *self, REFIID iid, void **out)
{
    if (!out) { return E_POINTER; } *out = NULL;
    if (IsEqualIID(iid,&IID_IUnknown) || IsEqualIID(iid,&font_iid_renderer) || IsEqualIID(iid,&font_iid_snapping)) {
        *out = self; ++((FontDraw *)self)->refs; return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE add_ref(DwRenderer *self) { return ++((FontDraw *)self)->refs; }
static ULONG STDMETHODCALLTYPE release(DwRenderer *self) { return --((FontDraw *)self)->refs; }
static HRESULT STDMETHODCALLTYPE snapping(DwRenderer *self, void *context, BOOL *disabled)
{
    (void)self; (void)context; *disabled = TRUE; return S_OK;
}
static HRESULT STDMETHODCALLTYPE transform(DwRenderer *self, void *context, DwMatrix *matrix)
{
    (void)self; (void)context; *matrix = (DwMatrix){1,0,0,1,0,0}; return S_OK;
}
static HRESULT STDMETHODCALLTYPE pixels_per_dip(DwRenderer *self, void *context, FLOAT *scale)
{
    (void)context; *scale = (float)((FontDraw *)self)->run->config.dpi / 96; return S_OK;
}
static HRESULT STDMETHODCALLTYPE glyph_run(DwRenderer *self, void *context, FLOAT x, FLOAT y, UINT mode,
    const DwGlyphRun *glyphs, const DwDescription *description, IUnknown *effect)
{
    (void)context; (void)description; (void)effect;
    FontDraw *draw = (FontDraw *)self; FontCache *c = draw->cache;
    if (draw->result != FONT_OK) { return E_FAIL; }
    if (glyphs->count > 4096 || draw->run->metrics.glyphs > 4096 - glyphs->count) {
        draw->result = FONT_FULL; return E_FAIL;
    }
    draw->run->metrics.glyphs += glyphs->count; ++draw->run->metrics.visual_runs;
    if (glyphs->bidi & 1) { ++draw->run->metrics.rtl_runs; }
    for (UINT32 i = 0; i < glyphs->count; ++i) { if (!glyphs->indices[i]) { ++draw->run->metrics.missing_glyphs; } }
    DwAnalysis *analysis = NULL;
    HRESULT hr = c->factory->lpVtbl->CreateGlyphRunAnalysis(c->factory,glyphs,(float)draw->run->config.dpi / 96,
        NULL,5,mode,x,y,&analysis);
    if (FAILED(hr)) { draw->result = FONT_OS_ERROR; return hr; }
    ++c->stats.com_objects; RECT bounds = {0};
    hr = analysis->lpVtbl->GetAlphaTextureBounds(analysis,1,&bounds);
    if (SUCCEEDED(hr)) {
        int64_t width = (int64_t)bounds.right - bounds.left, height = (int64_t)bounds.bottom - bounds.top;
        if (width < 0 || height < 0 || width > 1022 || height > 1022 ||
            (uint64_t)width * (uint64_t)height * 3 > FONT_SCRATCH_BYTES) { draw->result = FONT_FULL; hr = E_FAIL; }
        else if (width && height) {
            hr = analysis->lpVtbl->CreateAlphaTexture(analysis,1,&bounds,c->scratch,(UINT32)(width * height * 3));
            if (SUCCEEDED(hr)) { draw->result = font_atlas_add(c,draw->run,&bounds,c->scratch); }
        }
    }
    analysis->lpVtbl->Release(analysis); --c->stats.com_objects;
    if (FAILED(hr) && draw->result == FONT_OK) { draw->result = FONT_OS_ERROR; }
    return draw->result == FONT_OK ? S_OK : E_FAIL;
}
static HRESULT STDMETHODCALLTYPE decoration(DwRenderer *self, void *context, FLOAT x, FLOAT y,
    const void *data, IUnknown *effect)
{
    (void)context; (void)x; (void)y; (void)data; (void)effect;
    ((FontDraw *)self)->result = FONT_UNSUPPORTED; return E_NOTIMPL;
}
static HRESULT STDMETHODCALLTYPE inline_object(DwRenderer *self, void *context, FLOAT x, FLOAT y,
    DwInline *object, BOOL sideways, BOOL rtl, IUnknown *effect)
{
    return object->lpVtbl->Draw(object,context,self,x,y,sideways,rtl,effect);
}
const DwRendererVtbl font_draw_vtable = {query,add_ref,release,snapping,transform,pixels_per_dip,
    glyph_run,decoration,decoration,inline_object};
