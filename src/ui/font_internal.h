#ifndef SPOTIC_FONT_INTERNAL_H
#define SPOTIC_FONT_INTERNAL_H
#include "font.h"
#include "font_dwrite.h"
typedef struct FontDraw {
    DwRenderer iface;
    FontCache *cache;
    FontRun *run;
    DwMetrics metrics;
    ULONG refs;
    FontResult result;
} FontDraw;
bool font_owner(const FontCache *cache);
bool font_config_valid(const FontConfig *config);
bool font_key(const FontRun *run, Clay_StringSlice text, const FontConfig *config);
bool font_run_valid(const FontCache *cache, const FontRun *run);
FontResult font_shape(FontCache *cache, FontRun *run);
FontResult font_atlas_add(FontCache *cache, FontRun *run, const RECT *bounds, const BYTE *rgb);
extern const DwRendererVtbl font_draw_vtable;
void font_touch(FontCache *cache, FontRun *run);
void font_invalidate_clay(FontCache *cache);
#endif
