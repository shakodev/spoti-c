#ifndef SPOTIC_UI_ICONS_H
#define SPOTIC_UI_ICONS_H
#include "renderer.h"
/* Original procedural glyphs; values are the pixel-shader ABI. */
typedef enum UiIcon { UI_ICON_PLAY, UI_ICON_PAUSE, UI_ICON_PREVIOUS, UI_ICON_NEXT,
    UI_ICON_SHUFFLE, UI_ICON_REPEAT, UI_ICON_REPEAT_ONE, UI_ICON_VOLUME, UI_ICON_MUTE,
    UI_ICON_LYRICS, UI_ICON_QUEUE, UI_ICON_DEVICE, UI_ICON_MINIPLAYER, UI_ICON_FULLSCREEN } UiIcon;
UiRenderResult ui_icon_draw(UiRenderFrame *, UiRect, UiIcon, const float color[4]);
#endif
