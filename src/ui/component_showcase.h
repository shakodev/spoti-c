#ifndef SPOTIC_UI_COMPONENT_SHOWCASE_H
#define SPOTIC_UI_COMPONENT_SHOWCASE_H
#include "components.h"
#define UI_SHOWCASE_WIDTH 1040u
#define UI_SHOWCASE_HEIGHT 1000u
#define UI_SHOWCASE_STATES 6u
/* Draw inside active frame; capture at exact DIP size, never rescale PNGs.
 * Pointer should be outside the viewport for deterministic static states.
 * The six columns are normal/hover/pressed/selected/disabled/focus.
 * Artwork is original geometry in components.c, not a copied reference asset. */
void ui_component_showcase(UiWidgets *ui, bool rtl);
#endif
