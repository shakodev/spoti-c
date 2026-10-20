#ifndef SPOTIC_UI_FONT_ROLES_H
#define SPOTIC_UI_FONT_ROLES_H
#include "font.h"

/* Clay fontId 0/1 keep the original Segoe UI/Consolas behavior. Roles select
 * a semantic weight independently of the surrounding measure context. Sizes,
 * spacing and line heights remain logical DIP values on the Clay text config.
 * RTL applies to role IDs; legacy IDs preserve the context's direction. */
typedef enum FontRole {
    FONT_ROLE_BODY = 16, FONT_ROLE_BOLD, FONT_ROLE_TITLE, FONT_ROLE_HEADLINE
} FontRole;
#define FONT_ROLE_RTL 0x100u

/* FONT_FACE_NOTICE preserves the context's weight/direction but disables
 * compact-label trimming, as required by the status-message layout.
 * Same resolver for word measurement, app measurement caching and rendering.
 * Unknown IDs produce an invalid face, rejected by the normal font API. */
FontConfig font_config_for_clay(FontConfig base, uint16_t font_id,
    uint16_t size, uint16_t spacing, uint16_t line_height);
#endif
