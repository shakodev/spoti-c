#ifndef SPOTIC_UI_FONT_H
#define SPOTIC_UI_FONT_H
#include "renderer_clay.h"

#define FONT_TEXT_BYTES 2048u
#define FONT_RUNS 64u
#define FONT_PATCHES 64u
#define FONT_PENDING 7u
#define FONT_ATLAS_ENTRIES 128u
#define FONT_ATLAS_SIDE 1024u
#define FONT_SCRATCH_BYTES (256u * 1024u)
#define FONT_FACE_NOTICE 2u /* Clay semantic role; shaped with Segoe UI, without trimming. */

typedef enum FontResult { FONT_OK, FONT_PENDING_WORK, FONT_FULL, FONT_INVALID,
    FONT_STALE, FONT_OS_ERROR, FONT_UNSUPPORTED } FontResult;
typedef struct FontConfig {
    uint64_t text_generation;
    uint16_t face, size, dpi, weight;
    float letter_spacing, line_height, max_width;
    bool italic, rtl, ellipsis, measure_only;
} FontConfig;
typedef struct FontHandle { uint32_t slot; uint64_t generation; } FontHandle;
typedef struct FontMetrics {
    float width, height;
    uint32_t glyphs, visual_runs, rtl_runs, missing_glyphs, utf16_units;
    bool trimmed;
} FontMetrics;
typedef struct FontPatch {
    uint64_t generation;
    uint16_t entry;
    float x, y;
} FontPatch;
typedef struct FontRun {
    FontConfig config;
    FontMetrics metrics;
    FontPatch patches[FONT_PATCHES];
    uint64_t generation, used;
    uint32_t length, patch_count;
    FontResult failure; /* Negative cache prevents automatic retry/poll loops. */
    char text[FONT_TEXT_BYTES];
    bool valid;
} FontRun;
typedef struct FontAtlasEntry {
    uint64_t generation, used, pinned;
    uint16_t x, y, width, height, tile_x, tile_y, tile_w, tile_h;
    bool valid;
} FontAtlasEntry;
typedef struct FontRequest { FontConfig config; uint32_t length; char text[FONT_TEXT_BYTES]; } FontRequest;
typedef struct FontStats {
    uint64_t prepared, hits, misses, wakes, evictions, resets;
    uint32_t com_objects, pending, atlas_entries;
} FontStats;
typedef struct FontCache {
    struct DwFactory *factory;
    FontRun runs[FONT_RUNS];
    FontRequest pending[FONT_PENDING];
    FontAtlasEntry entries[FONT_ATLAS_ENTRIES];
    uint32_t tiles[32];
    unsigned char atlas[FONT_ATLAS_SIDE * FONT_ATLAS_SIDE];
    unsigned char scratch[FONT_SCRATCH_BYTES];
    uint16_t utf16[FONT_TEXT_BYTES + 1];
    uint64_t serial, clock, epoch, uploaded, prepare_id;
    uint32_t owner;
    UiTexture texture;
    UiRenderer *gpu_owner;
    Clay_Context *clay_context; /* Borrowed; must outlive the binding. */
    FontStats stats;
    void (*wake)(void *context);
    void *wake_context;
} FontCache;
_Static_assert(sizeof(((FontCache *)0)->scratch) <= 256u * 1024u, "glyph scratch budget");
_Static_assert(sizeof(((FontCache *)0)->runs)+sizeof(((FontCache *)0)->pending)+
    sizeof(((FontCache *)0)->entries)+sizeof(((FontCache *)0)->utf16)<=256u*1024u,
    "bounded font CPU records, descriptors and UTF-16 staging");
_Static_assert(sizeof(((FontCache *)0)->atlas) == 1024u * 1024u, "R8 atlas backing budget");
typedef struct FontMeasureContext { FontCache *cache; FontConfig config; FontResult last_result; } FontMeasureContext;

/* Caller-owned zero-initialized storage. UI-thread only; no copies while live.
 * DirectWrite allocations are confined to create/prepare/destroy. Font face 0
 * is Segoe UI, 1 is Consolas; weight/style are explicit. Emoji are monochrome. */
FontResult font_cache_create(FontCache *cache, void (*wake)(void *), void *context);
void font_cache_destroy(FontCache *cache);
FontResult font_prepare_text(FontCache *cache, Clay_StringSlice text, const FontConfig *config, FontHandle *out);
FontResult font_prepare_pending(FontCache *cache);
FontResult font_request_text(FontCache *cache, Clay_StringSlice text, const FontConfig *config);
FontResult font_lookup(FontCache *cache, Clay_StringSlice text, const FontConfig *config, FontHandle *out);
FontResult font_metrics(FontCache *cache, FontHandle handle, FontMetrics *out);
/* Safe preparation boundary ONLY. An alternate/fake backend can commit its
 * own R8 handle; it owns that handle. Renderer upload owns one 1 MiB R8 texture. */
FontResult font_upload_renderer(FontCache *cache, UiRenderer *renderer);
FontResult font_atlas_commit(FontCache *cache, UiTexture texture);
FontResult font_cache_reset(FontCache *cache);
FontResult font_render_text(FontCache *cache, FontHandle handle, UiRenderFrame *frame,
    float x, float y, const float color[4]);
Clay_Dimensions font_measure(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data);
FontConfig font_clay_config(const FontMeasureContext *, uint16_t face, uint16_t size, uint16_t spacing, uint16_t height);
/* Call after Clay_Initialize on its owning UI thread. */
void font_bind_clay(FontMeasureContext *context);
UiRenderResult font_render_clay(void *context, UiRenderFrame *frame, const Clay_RenderCommand *command);
FontStats font_stats(const FontCache *cache);
FontResult font_utf8_to_utf16(Clay_StringSlice text, uint16_t *output, uint32_t capacity, uint32_t *length);
#endif
