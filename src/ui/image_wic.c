#define COBJMACROS
#include "image_decode.h"
#include <wincodec.h>
#include <psapi.h>
#include <string.h>

static uint32_t read_le24(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static uint32_t read_le32(const unsigned char *p)
{
    return read_le24(p) | ((uint32_t)p[3] << 24);
}

/* Parse the bounded container before loading any installed codec. VP8X may
 * advertise a huge canvas despite a small embedded frame, so check both. */
static ArtResult webp_dimensions(const unsigned char *p, size_t bytes,
    uint32_t *width, uint32_t *height)
{
    if (bytes < 12 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WEBP", 4)) {
        return ART_DECODE_ERROR;
    }
    uint32_t riff = read_le32(p + 4);
    if (riff < 4 || (uint64_t)riff + 8 != bytes) { return ART_DECODE_ERROR; }
    uint32_t canvas_w = 0, canvas_h = 0, frame_w = 0, frame_h = 0;
    for (size_t at = 12; at < bytes;) {
        if (bytes - at < 8) { return ART_DECODE_ERROR; }
        const unsigned char *tag = p + at;
        size_t length = read_le32(tag + 4);
        at += 8;
        if (length > bytes - at || (length & 1u) > bytes - at - length) {
            return ART_DECODE_ERROR;
        }
        const unsigned char *data = p + at;
        uint32_t w = 0, h = 0;
        if (!memcmp(tag, "ANIM", 4) || !memcmp(tag, "ANMF", 4)) {
            return ART_UNSUPPORTED;
        }
        if (!memcmp(tag, "VP8X", 4)) {
            if (length != 10 || canvas_w || frame_w || (data[0] & 0xc1u) ||
                data[1] || data[2] || data[3]) { return ART_DECODE_ERROR; }
            if (data[0] & 2u) { return ART_UNSUPPORTED; }
            canvas_w = read_le24(data + 4) + 1;
            canvas_h = read_le24(data + 7) + 1;
            w = canvas_w; h = canvas_h;
        } else if (!memcmp(tag, "VP8L", 4)) {
            if (length < 5 || data[0] != 0x2f || frame_w) { return ART_DECODE_ERROR; }
            uint32_t bits = read_le32(data + 1);
            if (bits >> 29) { return ART_DECODE_ERROR; }
            frame_w = (bits & 0x3fffu) + 1;
            frame_h = ((bits >> 14) & 0x3fffu) + 1;
            w = frame_w; h = frame_h;
        } else if (!memcmp(tag, "VP8 ", 4)) {
            if (length < 10 || frame_w || (data[0] & 1u) ||
                memcmp(data + 3, "\x9d\x01\x2a", 3)) { return ART_DECODE_ERROR; }
            frame_w = ((uint32_t)data[6] | ((uint32_t)data[7] << 8)) & 0x3fffu;
            frame_h = ((uint32_t)data[8] | ((uint32_t)data[9] << 8)) & 0x3fffu;
            w = frame_w; h = frame_h;
        }
        if (w && (w > ART_ORIGINAL_EDGE || h > ART_ORIGINAL_EDGE ||
            (uint64_t)w * h > (uint64_t)ART_ORIGINAL_EDGE * ART_ORIGINAL_EDGE)) {
            return ART_TOO_LARGE;
        }
        at += length + (length & 1u);
    }
    if (!frame_w || !frame_h || (canvas_w &&
        (canvas_w != frame_w || canvas_h != frame_h))) { return ART_DECODE_ERROR; }
    *width = frame_w; *height = frame_h;
    return ART_OK;
}

static void memory_observation(ImageStats *stats, bool before)
{
    PROCESS_MEMORY_COUNTERS_EX memory = {0};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&memory,
        (DWORD)sizeof(memory))) {
        if (before) { stats->wic_private_before = memory.PrivateUsage; }
        else { stats->wic_private_after = memory.PrivateUsage; }
        /* Process-wide lifetime high-water mark, not attribution to this codec. */
        stats->wic_peak_private = memory.PeakPagefileUsage;
    }
}

static bool cancelled(const CancelToken *cancel)
{
    return cancel && worker_cancelled(cancel);
}

#define WIC_CHECK(expression) do { \
    if (cancelled(cancel)) { result = ART_CANCELLED; goto finish; } \
    HRESULT call_hr = (expression); \
    if (FAILED(call_hr)) { goto finish; } \
    if (cancelled(cancel)) { result = ART_CANCELLED; goto finish; } \
} while (0)
#define WIC_RELEASE(object) do { \
    if (object) { IUnknown_Release((IUnknown *)(object)); --stats->live_objects; } \
} while (0)

ArtResult image_decode_webp(const void *input, size_t bytes, unsigned char *rgba,
    uint32_t edge, const CancelToken *cancel, uint32_t *width, uint32_t *height,
    ImageStats *stats, bool allow_codec)
{
    if (!stats) { return ART_INVALID; }
    *stats = (ImageStats){0};
    if (width) { *width = 0; } if (height) { *height = 0; }
    if (!input || !rgba || !width || !height || !edge || edge > ART_MAX_EDGE) {
        return ART_INVALID;
    }
    if (bytes > ART_COMPRESSED_BYTES) { return ART_TOO_LARGE; }
    if (cancelled(cancel)) { return ART_CANCELLED; }
    uint32_t original_w = 0, original_h = 0;
    ArtResult result = webp_dimensions(input, bytes, &original_w, &original_h);
    if (result != ART_OK) { return result; }
    if (!allow_codec) { return ART_UNSUPPORTED; }

    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    bool uninitialize = false;
    memory_observation(stats, true);
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) { uninitialize = true; }
    else if (hr != RPC_E_CHANGED_MODE) { return ART_UNSUPPORTED; }
    result = ART_UNSUPPORTED;
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory, (void **)&factory);
    if (factory) { ++stats->live_objects; }
    if (FAILED(hr) || cancelled(cancel)) { goto finish; }
    /* Request this container's installed decoder explicitly: a generic stream
     * sniff is insufficient to distinguish absent support from invalid pixels. */
    hr = IWICImagingFactory_CreateDecoder(factory, &GUID_ContainerFormatWebp, NULL, &decoder);
    if (decoder) { ++stats->live_objects; }
    if (FAILED(hr)) { goto finish; }
    stats->wic_available = true;
    result = ART_DECODE_ERROR;
    if (cancelled(cancel)) { result = ART_CANCELLED; goto finish; }
    hr = IWICImagingFactory_CreateStream(factory, &stream);
    if (stream) { ++stats->live_objects; }
    if (FAILED(hr)) { goto finish; }
    WIC_CHECK(IWICStream_InitializeFromMemory(stream, (BYTE *)(uintptr_t)input, (DWORD)bytes));
    WIC_CHECK(IWICBitmapDecoder_Initialize(decoder, (IStream *)stream, WICDecodeMetadataCacheOnDemand));
    UINT frames = 0;
    WIC_CHECK(IWICBitmapDecoder_GetFrameCount(decoder, &frames));
    if (frames != 1) { result = ART_UNSUPPORTED; goto finish; }
    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (frame) { ++stats->live_objects; }
    if (FAILED(hr)) { goto finish; }
    UINT source_w = 0, source_h = 0;
    WIC_CHECK(IWICBitmapFrameDecode_GetSize(frame, &source_w, &source_h));
    if (source_w != original_w || source_h != original_h) { goto finish; }
    uint32_t max_edge = original_w > original_h ? original_w : original_h;
    uint32_t output_w = original_w, output_h = original_h;
    IWICBitmapSource *source = (IWICBitmapSource *)frame; /* Borrowed. */
    if (max_edge > edge) {
        output_w = original_w * edge / max_edge;
        output_h = original_h * edge / max_edge;
        if (!output_w) { output_w = 1; } if (!output_h) { output_h = 1; }
        hr = IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
        if (scaler) { ++stats->live_objects; }
        if (FAILED(hr)) { goto finish; }
        WIC_CHECK(IWICBitmapScaler_Initialize(scaler, source, output_w, output_h,
            WICBitmapInterpolationModeNearestNeighbor));
        source = (IWICBitmapSource *)scaler;
    }
    hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (converter) { ++stats->live_objects; }
    if (FAILED(hr)) { goto finish; }
    WIC_CHECK(IWICFormatConverter_Initialize(converter, source, &GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom));
    WIC_CHECK(IWICFormatConverter_CopyPixels(converter, NULL, output_w * 4,
        output_w * output_h * 4, rgba));
    *width = output_w; *height = output_h;
    result = ART_OK;
finish:
    if (cancelled(cancel)) { result = ART_CANCELLED; *width = 0; *height = 0; }
    WIC_RELEASE(converter); WIC_RELEASE(scaler); WIC_RELEASE(frame);
    WIC_RELEASE(decoder); WIC_RELEASE(stream); WIC_RELEASE(factory);
    if (uninitialize) { CoUninitialize(); }
    memory_observation(stats, false);
    return result;
}
