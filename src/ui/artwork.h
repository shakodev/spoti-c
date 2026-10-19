#ifndef SPOTIC_ARTWORK_H
#define SPOTIC_ARTWORK_H
#include "image_decode.h"
#include "renderer.h"
#define ART_REQUESTS 8u
#define ART_CACHE_SLOTS 32u
#define ART_GPU_BYTES (4u * 1024u * 1024u)
#define ART_URL_CHARS 1024u
typedef struct ArtworkId { uint8_t content[20]; uint32_t size; uint64_t generation; } ArtworkId;
typedef struct ArtworkHandle { uint32_t slot; uint64_t serial; } ArtworkHandle;
typedef struct ArtworkSource { const wchar_t *url; uint32_t width, height; } ArtworkSource;
typedef struct ArtworkGpu {
    ArtResult (*upload)(void *, uint32_t, uint32_t, const unsigned char *, UiTexture *);
    void (*release)(void *, UiTexture);
    bool (*valid)(void *, UiTexture);
    void *context;
} ArtworkGpu;
typedef struct TextureEntry {
    ArtworkId id;
    UiTexture texture;
    uint32_t bytes;
    uint64_t serial, touched;
    bool pinned, retired;
} TextureEntry;
typedef struct TextureCache {
    TextureEntry entries[ART_CACHE_SLOTS];
    ArtworkGpu gpu;
    uint64_t serial, clock, uploads, releases;
    uint32_t bytes, peak_bytes;
} TextureCache;
bool artwork_id_equal(ArtworkId a, ArtworkId b);
/* All cache operations on UI owner only. prepare ends use of the PREVIOUS frame,
 * releases retired resources and unpins. Resolve pins for the next frame.
 * Insert may retire LRU entries but returns FULL until next prepare: resources
 * referenced by an already-built frame cannot be destroyed underneath it. */
void texture_cache_init(TextureCache *cache, ArtworkGpu gpu);
void texture_cache_prepare(TextureCache *cache);
void texture_cache_collect(TextureCache *cache); /* Safe boundary, preserves visibility pins. */
ArtResult texture_cache_insert(TextureCache *cache, ArtworkId id, uint32_t width,
    uint32_t height, const unsigned char *rgba, ArtworkHandle *out);
bool texture_cache_find(TextureCache *cache, ArtworkId id, ArtworkHandle *out);
bool texture_cache_resolve(TextureCache *cache, ArtworkHandle handle, UiTexture *out);
void texture_cache_clear(TextureCache *cache);
ArtworkGpu artwork_renderer_gpu(UiRenderer *renderer);

typedef ArtResult (*ArtworkFetch)(void *context, const wchar_t *url, void *buffer,
    size_t capacity, size_t *bytes, const CancelToken *cancel);
typedef struct Artwork Artwork;
typedef struct ArtworkConfig {
    Worker *worker; /* Borrowed, exclusively serialized art jobs; outlives stop. */
    ArtworkFetch fetch; void *fetch_context;
    ArtworkGpu gpu;
    void *decode_workspace; /* >=512 KiB, 16-byte aligned, setup-owned. */
    unsigned char *upload; /* >=256 KiB, setup-owned. */
    Metrics *metrics;
} ArtworkConfig;
typedef struct ArtworkStats {
    uint64_t submitted, completed, uploaded, stale, cancelled;
    uint32_t pending, gpu_bytes, peak_gpu_bytes;
    bool active;
    ImageStats decode;
} ArtworkStats;
size_t artwork_size(void);
size_t artwork_alignment(void);
/* Fresh caller storage, no heap. init/stop outside frames. Every other API is
 * UI owner only; fetch/decode execute exclusively on the supplied worker. */
ArtResult artwork_init(void *storage, size_t bytes, const ArtworkConfig *config, Artwork **out);
/* Navigation generations increase monotonically; zero/decreasing values fail. */
ArtResult artwork_navigate(Artwork *art, uint64_t generation);
/* Safe boundary after prior frame consumption: start a new visible set, then
 * lookup/request every visible image BEFORE prepare. Pins otherwise persist.
 * This prevents async uploads from evicting the currently visible textures. */
ArtResult artwork_begin_visible(Artwork *art);
ArtResult artwork_request(Artwork *art, ArtworkId id, const ArtworkSource *sources, size_t count);
/* Safe preparation boundary BEFORE frame construction. Processes at most one
 * upload and starts at most one worker job. Never fetches/decodes synchronously. */
ArtResult artwork_prepare(Artwork *art);
bool artwork_lookup(Artwork *art, ArtworkId id, ArtworkHandle *out);
bool artwork_resolve(Artwork *art, ArtworkHandle handle, UiTexture *out);
/* Frame-only: pins a ready image or emits a solid gray placeholder. No network,
 * decoder, allocator, GPU creation or release can run through this function. */
UiRenderResult artwork_draw(Artwork *art, ArtworkId id, UiRenderFrame *frame, UiRect box);
HANDLE artwork_wait_handle(const Artwork *art); /* borrowed, never reset externally */
ArtworkStats artwork_stats(Artwork *art);
/* Cancel admission/work then return PENDING until callback has retired. Keep
 * storage, worker and buffers live and call stop again after wake. */
ArtResult artwork_stop(Artwork *art);
/* Production fetch: optional context=Metrics*. HTTPS only, no redirects/auth,
 * <=64 KiB, 1 s native phase timeouts and 5 s cooperative total deadline;
 * worker cancellation checked between synchronous WinHTTP calls. OS calls
 * cannot be preempted by the token. All handles retire before return. */
ArtResult artwork_http_fetch(void *context, const wchar_t *url, void *buffer,
    size_t capacity, size_t *bytes, const CancelToken *cancel);
#endif
