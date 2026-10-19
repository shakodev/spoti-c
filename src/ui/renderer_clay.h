#ifndef SPOTIC_UI_RENDERER_CLAY_H
#define SPOTIC_UI_RENDERER_CLAY_H
#include "renderer.h"
#include "clay.h"
typedef struct UiClayBridge {
    UiRenderResult (*image)(void *context, void *image_data, UiTexture *texture);
    UiRenderResult (*custom)(void *context, UiRenderFrame *frame, const Clay_RenderCommand *command);
    void *context;
} UiClayBridge;
UiRenderResult ui_frame_clay(UiRenderFrame *frame, Clay_RenderCommandArray commands, const UiClayBridge *bridge);
#endif
