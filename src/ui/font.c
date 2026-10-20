#include "font_internal.h"
#include <math.h>

const IID font_iid_factory = {0xb859ee5a,0xd838,0x4b5b,{0xa2,0xe8,0x1a,0xdc,0x7d,0x93,0xdb,0x48}};
const IID font_iid_layout1 = {0x9064d822,0x80a7,0x465c,{0xa9,0x86,0xdf,0x65,0xf7,0x8b,0x8f,0xeb}};
const IID font_iid_renderer = {0xef8a8135,0x5cc6,0x45fe,{0x88,0x25,0xc5,0xa0,0x72,0x4e,0xb8,0x19}};
const IID font_iid_snapping = {0xeaf3a2da,0xecf4,0x4d24,{0xb6,0x44,0xb3,0x4f,0x68,0x42,0x02,0x4b}};

FontResult font_utf8_to_utf16(Clay_StringSlice text, uint16_t *output, uint32_t capacity, uint32_t *length)
{
    if (!length) { return FONT_INVALID; } *length = 0;
    if (text.length < 0 || (text.length && !text.chars) || (!output && capacity)) { return FONT_INVALID; }
    if (text.length > (int32_t)FONT_TEXT_BYTES) { return FONT_FULL; }
    const unsigned char *s = (const unsigned char *)text.chars;
    uint32_t used = 0;
    for (uint32_t at = 0; at < (uint32_t)text.length;) {
        uint32_t code = s[at++], extra = 0, minimum = 0;
        if (code < 128) { }
        else if (code >= 0xc2 && code <= 0xdf) { code &= 31; extra = 1; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { code &= 15; extra = 2; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { code &= 7; extra = 3; minimum = 0x10000; }
        else { return FONT_INVALID; }
        if (extra > (uint32_t)text.length - at) { return FONT_INVALID; }
        for (uint32_t i = 0; i < extra; ++i) {
            if ((s[at] & 0xc0) != 0x80) { return FONT_INVALID; }
            code = (code << 6) | (s[at++] & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) { return FONT_INVALID; }
        uint32_t count = code >= 0x10000 ? 2u : 1u;
        if (output && count > capacity - used) { return FONT_FULL; }
        if (output) {
            if (count == 2) {
                output[used] = (uint16_t)(0xd800 + ((code - 0x10000) >> 10));
                output[used + 1] = (uint16_t)(0xdc00 + ((code - 0x10000) & 1023));
            } else { output[used] = (uint16_t)code; }
        }
        used += count;
    }
    if (output && used < capacity) { output[used] = 0; }
    *length = used; return FONT_OK;
}

FontResult font_cache_create(FontCache *c, void (*wake)(void *), void *context)
{
    if (!c || c->factory) { return FONT_INVALID; }
    HRESULT hr = DWriteCreateFactory(0,&font_iid_factory,(IUnknown **)&c->factory);
    if (FAILED(hr)) { return FONT_OS_ERROR; }
    c->owner = GetCurrentThreadId(); c->wake = wake; c->wake_context = context;
    c->stats.com_objects = 1; c->epoch = 1; return FONT_OK;
}
void font_cache_destroy(FontCache *c)
{
    if (!font_owner(c)) { return; }
    if (c->gpu_owner && c->texture) { (void)ui_renderer_texture_release(c->gpu_owner,c->texture); }
    c->gpu_owner = NULL; c->texture = 0;
    if (c->clay_context) {
        Clay_Context *before = Clay_GetCurrentContext(); Clay_SetCurrentContext(c->clay_context);
        Clay_SetMeasureTextFunction(font_measure,NULL); Clay_ResetMeasureTextCache(); Clay_SetCurrentContext(before);
    }
    c->clay_context = NULL;
    c->factory->lpVtbl->Release(c->factory); c->factory = NULL; --c->stats.com_objects;
    for (unsigned i = 0; i < FONT_RUNS; ++i) { c->runs[i].valid = false; }
    c->stats.pending = 0;
}

#define FONT_RELEASE(c, object) do { if (object) { (object)->lpVtbl->Release(object); \
    object = NULL; --(c)->stats.com_objects; } } while (0)

FontResult font_shape(FontCache *c, FontRun *run)
{
    FontConfig *key = &run->config; DwFormat *format = NULL; DwLayout *layout = NULL, *layout1 = NULL;
    DwInline *ellipsis = NULL; DwTypography *typography = NULL; DwFactory *factory = c->factory;
    HRESULT hr = factory->lpVtbl->CreateTextFormat(factory,key->face ? L"Consolas" : L"Segoe UI",NULL,
        key->weight,key->italic ? 2u : 0u,5,(FLOAT)key->size,L"en-us",&format);
    if (SUCCEEDED(hr)) { ++c->stats.com_objects; hr = format->lpVtbl->SetWordWrapping(format,1); }
    if (SUCCEEDED(hr)) { hr = format->lpVtbl->SetReadingDirection(format,key->rtl ? 1u : 0u); }
    if (SUCCEEDED(hr) && key->line_height > 0) {
        hr = format->lpVtbl->SetLineSpacing(format,1,key->line_height,key->line_height * 0.8f);
    }
    if (SUCCEEDED(hr) && key->ellipsis) {
        hr = factory->lpVtbl->CreateEllipsisTrimmingSign(factory,format,&ellipsis);
        if (SUCCEEDED(hr)) {
            ++c->stats.com_objects; DwTrimming trim = {1,0,0};
            hr = format->lpVtbl->SetTrimming(format,&trim,ellipsis);
        }
    }
    if (SUCCEEDED(hr)) {
        hr = factory->lpVtbl->CreateTextLayout(factory,(const WCHAR *)c->utf16,run->metrics.utf16_units,
            format,key->max_width > 0 ? key->max_width : 65536,65536,&layout);
        if (SUCCEEDED(hr)) { ++c->stats.com_objects; }
    }
    if (SUCCEEDED(hr) && key->letter_spacing > 0) {
        hr = layout->lpVtbl->QueryInterface(layout,&font_iid_layout1,(void **)&layout1);
        if (SUCCEEDED(hr)) {
            ++c->stats.com_objects;
            hr = layout1->lpVtbl->SetCharacterSpacing(layout1,0,key->letter_spacing,0,(DwRange){0,run->metrics.utf16_units});
        }
    }
    if (SUCCEEDED(hr)) {
        hr = factory->lpVtbl->CreateTypography(factory,&typography);
        if (SUCCEEDED(hr)) { ++c->stats.com_objects; }
        const UINT32 tags[] = {0x6167696c,0x67696c63,0x67696c72}; /* liga, clig, rlig */
        for (unsigned i = 0; i < 3 && SUCCEEDED(hr); ++i) {
            hr = typography->lpVtbl->AddFontFeature(typography,(DwFeature){tags[i],1});
        }
        if (SUCCEEDED(hr)) { hr = layout->lpVtbl->SetTypography(layout,typography,(DwRange){0,run->metrics.utf16_units}); }
    }
    DwMetrics metrics = {0}; DwLineMetrics lines[64]; UINT32 count = 0;
    if (SUCCEEDED(hr)) { hr = layout->lpVtbl->GetMetrics(layout,&metrics); }
    if (SUCCEEDED(hr)) { hr = layout->lpVtbl->GetLineMetrics(layout,lines,64,&count); }
    FontResult result = FONT_OS_ERROR;
    if (SUCCEEDED(hr)) {
        run->metrics.width = metrics.trailing_width; run->metrics.height = metrics.height;
        for (UINT32 i = 0; i < count; ++i) { if (lines[i].trimmed) { run->metrics.trimmed = true; } }
        if (key->ellipsis) { run->metrics.width = fminf(run->metrics.width,key->max_width); }
        result=FONT_OK;
        if (!key->measure_only) {
            FontDraw renderer = {{&font_draw_vtable},c,run,metrics,1,FONT_OK};
            hr = layout->lpVtbl->Draw(layout,NULL,&renderer.iface,0,0);
            result = renderer.result != FONT_OK ? renderer.result : SUCCEEDED(hr) ? FONT_OK : FONT_OS_ERROR;
        }
        for (uint32_t i = 0; i < run->patch_count; ++i) {
            run->patches[i].x -= metrics.left; run->patches[i].y -= metrics.top;
        }
    } else if (hr == HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)) { result = FONT_FULL; }
    FONT_RELEASE(c,layout1); FONT_RELEASE(c,layout); FONT_RELEASE(c,ellipsis); FONT_RELEASE(c,format);
    FONT_RELEASE(c,typography);
    return result;
}
