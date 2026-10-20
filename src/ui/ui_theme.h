#ifndef SPOTIC_UI_THEME_H
#define SPOTIC_UI_THEME_H
#include "clay.h"
#include <stdint.h>

/* Immutable DIP tokens. See docs/specs/spotify-desktop-parity.md for exact
 * package provenance and the native typography substitution. No DPI here. */
typedef struct UiTheme {
    Clay_Color frame, panel, elevated, hover, pressed, text, subdued;
    Clay_Color accent, accent_hover, accent_pressed, on_accent;
    Clay_Color negative, warning, information, outline, decorative;
    Clay_Color tinted, tinted_hover, tinted_pressed, focus;
    uint16_t space[12], radius[5], control[3], icon[8], duration_ms[6];
    uint16_t body, small, marginal, title, large_title, headline, focus_width;
    float disabled_opacity, active_opacity, hover_scale;
} UiTheme;
const UiTheme *ui_theme(void);
Clay_Color ui_theme_opacity(Clay_Color color, float opacity);
#endif
