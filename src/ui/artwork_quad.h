#ifndef SPOTIC_UI_ARTWORK_QUAD_H
#define SPOTIC_UI_ARTWORK_QUAD_H
#include "renderer.h"
#include "clay.h"
#define UI_ARTWORK_COMMAND_MAGIC 0x41525431u
typedef struct UiArtworkCommand {
    uint32_t magic;
    UiTexture texture;
    uint32_t source_width, source_height;
    float radius, opacity;
} UiArtworkCommand;
extern const uint32_t ui_artwork_command_tag;
/* Borrowed texture; bounds and radius in DIP. Centered cover crop preserves
 * source aspect. Actual texture lifetime/generation validated at GPU submit. */
UiRenderResult ui_artwork_draw(UiRenderFrame *frame, UiTexture texture,
    uint32_t source_width, uint32_t source_height, UiRect bounds, float radius);
UiRenderResult ui_artwork_dispatch(UiRenderFrame *frame,
    const Clay_RenderCommand *command, bool *handled);
#endif
