#ifndef SPOTIC_FONT_DWRITE_H
#define SPOTIC_FONT_DWRITE_H
/* C ABI subset of Windows SDK dwrite.h/dwrite_1.h. Unused slots retain their
 * exact inherited positions; no C++ compilation, SDK header rewriting or casts
 * between called function types. STDMETHODCALLTYPE is required on every slot. */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define COBJMACROS
#include <windows.h>
#include <unknwn.h>
#include <stddef.h>
typedef struct DwFactory DwFactory;
typedef struct DwFormat DwFormat;
typedef struct DwLayout DwLayout;
typedef struct DwAnalysis DwAnalysis;
typedef struct DwInline DwInline;
typedef struct DwRenderer DwRenderer;
typedef struct DwTypography DwTypography;
typedef struct DwFeature { UINT32 tag, parameter; } DwFeature;
typedef struct DwRange { UINT32 start, length; } DwRange;
typedef struct DwMatrix { FLOAT m11,m12,m21,m22,dx,dy; } DwMatrix;
typedef struct DwOffset { FLOAT advance, ascender; } DwOffset;
typedef struct DwGlyphRun {
    IUnknown *face; FLOAT size; UINT32 count;
    const UINT16 *indices; const FLOAT *advances; const DwOffset *offsets;
    BOOL sideways; UINT32 bidi;
} DwGlyphRun;
typedef struct DwDescription {
    const WCHAR *locale, *text;
    UINT32 length;
    const UINT16 *clusters;
    UINT32 position;
} DwDescription;
typedef struct DwMetrics {
    FLOAT left,top,width,trailing_width,height,layout_width,layout_height;
    UINT32 bidi_depth,lines;
} DwMetrics;
typedef struct DwLineMetrics { UINT32 length, whitespace, newline; FLOAT height, baseline; BOOL trimmed; } DwLineMetrics;
typedef struct DwTrimming { UINT granularity; UINT32 delimiter, count; } DwTrimming;
#define DW_UNKNOWN(T) HRESULT (STDMETHODCALLTYPE *QueryInterface)(T *,REFIID,void **); \
    ULONG (STDMETHODCALLTYPE *AddRef)(T *); ULONG (STDMETHODCALLTYPE *Release)(T *)
#define DW_SKIP(name) void (STDMETHODCALLTYPE *name)(void)
typedef struct DwFactoryVtbl {
    DW_UNKNOWN(DwFactory);
    DW_SKIP(GetSystemFontCollection); DW_SKIP(CreateCustomFontCollection);
    DW_SKIP(RegisterFontCollectionLoader); DW_SKIP(UnregisterFontCollectionLoader);
    DW_SKIP(CreateFontFileReference); DW_SKIP(CreateCustomFontFileReference); DW_SKIP(CreateFontFace);
    DW_SKIP(CreateRenderingParams); DW_SKIP(CreateMonitorRenderingParams); DW_SKIP(CreateCustomRenderingParams);
    DW_SKIP(RegisterFontFileLoader); DW_SKIP(UnregisterFontFileLoader);
    HRESULT (STDMETHODCALLTYPE *CreateTextFormat)(DwFactory *,const WCHAR *,void *,UINT,UINT,UINT,FLOAT,const WCHAR *,DwFormat **);
    HRESULT (STDMETHODCALLTYPE *CreateTypography)(DwFactory *,DwTypography **);
    DW_SKIP(GetGdiInterop);
    HRESULT (STDMETHODCALLTYPE *CreateTextLayout)(DwFactory *,const WCHAR *,UINT32,DwFormat *,FLOAT,FLOAT,DwLayout **);
    DW_SKIP(CreateGdiCompatibleTextLayout);
    HRESULT (STDMETHODCALLTYPE *CreateEllipsisTrimmingSign)(DwFactory *,DwFormat *,DwInline **);
    DW_SKIP(CreateTextAnalyzer); DW_SKIP(CreateNumberSubstitution);
    HRESULT (STDMETHODCALLTYPE *CreateGlyphRunAnalysis)(DwFactory *,const DwGlyphRun *,FLOAT,const DwMatrix *,UINT,UINT,FLOAT,FLOAT,DwAnalysis **);
} DwFactoryVtbl;
struct DwFactory { const DwFactoryVtbl *lpVtbl; };
#define DW_FORMAT_SLOTS(T) DW_UNKNOWN(T); \
    DW_SKIP(SetTextAlignment); DW_SKIP(SetParagraphAlignment); \
    HRESULT (STDMETHODCALLTYPE *SetWordWrapping)(T *,UINT); \
    HRESULT (STDMETHODCALLTYPE *SetReadingDirection)(T *,UINT); \
    DW_SKIP(SetFlowDirection); DW_SKIP(SetIncrementalTabStop); \
    HRESULT (STDMETHODCALLTYPE *SetTrimming)(T *,const DwTrimming *,DwInline *); \
    HRESULT (STDMETHODCALLTYPE *SetLineSpacing)(T *,UINT,FLOAT,FLOAT); \
    DW_SKIP(GetTextAlignment); DW_SKIP(GetParagraphAlignment); DW_SKIP(GetWordWrapping); \
    DW_SKIP(GetReadingDirection); DW_SKIP(GetFlowDirection); DW_SKIP(GetIncrementalTabStop); \
    DW_SKIP(GetTrimming); DW_SKIP(GetLineSpacing); DW_SKIP(GetFontCollection); \
    DW_SKIP(GetFontFamilyNameLength); DW_SKIP(GetFontFamilyName); DW_SKIP(GetFontWeight); \
    DW_SKIP(GetFontStyle); DW_SKIP(GetFontStretch); DW_SKIP(GetFontSize); \
    DW_SKIP(GetLocaleNameLength); DW_SKIP(GetLocaleName)
typedef struct DwFormatVtbl { DW_FORMAT_SLOTS(DwFormat); } DwFormatVtbl;
struct DwFormat { const DwFormatVtbl *lpVtbl; };
typedef struct DwLayoutVtbl {
    DW_FORMAT_SLOTS(DwLayout);
    DW_SKIP(SetMaxWidth); DW_SKIP(SetMaxHeight); DW_SKIP(SetFontCollection);
    DW_SKIP(SetFontFamilyName); DW_SKIP(SetFontWeight); DW_SKIP(SetFontStyle);
    DW_SKIP(SetFontStretch); DW_SKIP(SetFontSize); DW_SKIP(SetUnderline); DW_SKIP(SetStrikethrough);
    DW_SKIP(SetDrawingEffect); DW_SKIP(SetInlineObject);
    HRESULT (STDMETHODCALLTYPE *SetTypography)(DwLayout *,DwTypography *,DwRange);
    DW_SKIP(SetLocaleName);
    DW_SKIP(GetMaxWidth); DW_SKIP(GetMaxHeight); DW_SKIP(GetFontCollectionRange);
    DW_SKIP(GetFontFamilyNameLengthRange); DW_SKIP(GetFontFamilyNameRange); DW_SKIP(GetFontWeightRange);
    DW_SKIP(GetFontStyleRange); DW_SKIP(GetFontStretchRange); DW_SKIP(GetFontSizeRange);
    DW_SKIP(GetUnderline); DW_SKIP(GetStrikethrough); DW_SKIP(GetDrawingEffect); DW_SKIP(GetInlineObject);
    DW_SKIP(GetTypography); DW_SKIP(GetLocaleNameLengthRange); DW_SKIP(GetLocaleNameRange);
    HRESULT (STDMETHODCALLTYPE *Draw)(DwLayout *,void *,DwRenderer *,FLOAT,FLOAT);
    HRESULT (STDMETHODCALLTYPE *GetLineMetrics)(DwLayout *,DwLineMetrics *,UINT32,UINT32 *);
    HRESULT (STDMETHODCALLTYPE *GetMetrics)(DwLayout *,DwMetrics *);
    DW_SKIP(GetOverhangMetrics); DW_SKIP(GetClusterMetrics); DW_SKIP(DetermineMinWidth);
    DW_SKIP(HitTestPoint); DW_SKIP(HitTestTextPosition); DW_SKIP(HitTestTextRange);
    /* IDWriteTextLayout1 tail */
    DW_SKIP(SetPairKerning); DW_SKIP(GetPairKerning);
    HRESULT (STDMETHODCALLTYPE *SetCharacterSpacing)(DwLayout *,FLOAT,FLOAT,FLOAT,DwRange);
    DW_SKIP(GetCharacterSpacing);
} DwLayoutVtbl;
struct DwLayout { const DwLayoutVtbl *lpVtbl; };
typedef struct DwAnalysisVtbl {
    DW_UNKNOWN(DwAnalysis);
    HRESULT (STDMETHODCALLTYPE *GetAlphaTextureBounds)(DwAnalysis *,UINT,RECT *);
    HRESULT (STDMETHODCALLTYPE *CreateAlphaTexture)(DwAnalysis *,UINT,const RECT *,BYTE *,UINT32);
    DW_SKIP(GetAlphaBlendParams);
} DwAnalysisVtbl;
struct DwAnalysis { const DwAnalysisVtbl *lpVtbl; };
typedef struct DwRendererVtbl {
    DW_UNKNOWN(DwRenderer);
    HRESULT (STDMETHODCALLTYPE *IsPixelSnappingDisabled)(DwRenderer *,void *,BOOL *);
    HRESULT (STDMETHODCALLTYPE *GetCurrentTransform)(DwRenderer *,void *,DwMatrix *);
    HRESULT (STDMETHODCALLTYPE *GetPixelsPerDip)(DwRenderer *,void *,FLOAT *);
    HRESULT (STDMETHODCALLTYPE *DrawGlyphRun)(DwRenderer *,void *,FLOAT,FLOAT,UINT,const DwGlyphRun *,const DwDescription *,IUnknown *);
    HRESULT (STDMETHODCALLTYPE *DrawUnderline)(DwRenderer *,void *,FLOAT,FLOAT,const void *,IUnknown *);
    HRESULT (STDMETHODCALLTYPE *DrawStrikethrough)(DwRenderer *,void *,FLOAT,FLOAT,const void *,IUnknown *);
    HRESULT (STDMETHODCALLTYPE *DrawInlineObject)(DwRenderer *,void *,FLOAT,FLOAT,DwInline *,BOOL,BOOL,IUnknown *);
} DwRendererVtbl;
struct DwRenderer { const DwRendererVtbl *lpVtbl; };
typedef struct DwInlineVtbl {
    DW_UNKNOWN(DwInline);
    HRESULT (STDMETHODCALLTYPE *Draw)(DwInline *,void *,DwRenderer *,FLOAT,FLOAT,BOOL,BOOL,IUnknown *);
    DW_SKIP(GetMetrics); DW_SKIP(GetOverhangMetrics); DW_SKIP(GetBreakConditions);
} DwInlineVtbl;
struct DwInline { const DwInlineVtbl *lpVtbl; };
typedef struct DwTypographyVtbl {
    DW_UNKNOWN(DwTypography);
    HRESULT (STDMETHODCALLTYPE *AddFontFeature)(DwTypography *,DwFeature);
    DW_SKIP(GetFontFeatureCount); DW_SKIP(GetFontFeature);
} DwTypographyVtbl;
struct DwTypography { const DwTypographyVtbl *lpVtbl; };
__declspec(dllimport) HRESULT WINAPI DWriteCreateFactory(UINT,REFIID,IUnknown **);
extern const IID font_iid_factory, font_iid_layout1, font_iid_renderer, font_iid_snapping;
#undef DW_UNKNOWN
#undef DW_SKIP
#undef DW_FORMAT_SLOTS
_Static_assert(offsetof(DwFactoryVtbl,CreateTextFormat) == 15 * sizeof(void *), "SDK factory ABI");
_Static_assert(offsetof(DwFactoryVtbl,CreateGlyphRunAnalysis) == 23 * sizeof(void *), "SDK analysis ABI");
_Static_assert(offsetof(DwLayoutVtbl,Draw) == 58 * sizeof(void *), "SDK layout ABI");
_Static_assert(offsetof(DwLayoutVtbl,SetCharacterSpacing) == 69 * sizeof(void *), "SDK layout1 ABI");
#endif
