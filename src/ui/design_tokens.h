#ifndef SPOTIC_UI_DESIGN_TOKENS_H
#define SPOTIC_UI_DESIGN_TOKENS_H
#include "clay.h"

/* Historical Encore dark base set; provenance/limits in spotify-desktop-parity.md.
 * Logical pixels throughout. No proprietary fonts, icon assets or animation. */
#define UI_CANVAS ((Clay_Color){0,0,0,255})
#define UI_PANEL ((Clay_Color){18,18,18,255})
#define UI_HIGHLIGHT ((Clay_Color){31,31,31,255})
#define UI_ELEVATED_HOVER ((Clay_Color){42,42,42,255})
#define UI_TEXT ((Clay_Color){255,255,255,255})
#define UI_SUBDUED ((Clay_Color){179,179,179,255})
#define UI_DISABLED ((Clay_Color){124,124,124,255})
#define UI_ACCENT ((Clay_Color){30,215,96,255})
#define UI_ACCENT_HOVER ((Clay_Color){63,230,120,255})
#define UI_NEGATIVE ((Clay_Color){243,114,127,255})
#define UI_WARNING ((Clay_Color){255,164,43,255})
enum { UI_SPACE_XS=4, UI_SPACE_SM=8, UI_SPACE_MD=12, UI_SPACE=16,
    UI_SPACE_LG=24, UI_SPACE_XL=32, UI_PANEL_RADIUS=8, UI_CARD_RADIUS=6,
    UI_TARGET=32, UI_PRIMARY_TARGET=56, UI_TRACK_HEIGHT=56, UI_LYRIC_HEIGHT=64 };
#endif
