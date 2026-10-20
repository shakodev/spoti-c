#include "font_roles.h"

FontConfig font_config_for_clay(FontConfig base, uint16_t font_id,
    uint16_t size, uint16_t spacing, uint16_t line_height)
{
    base.size = size; base.letter_spacing = spacing; base.line_height = line_height;
    if (font_id <= 1) { base.face = font_id; return base; }
    if (font_id == FONT_FACE_NOTICE) {
        base.face = 0; base.max_width = 0; base.ellipsis = false; return base;
    }
    uint16_t role = (uint16_t)(font_id & (UINT16_MAX ^ FONT_ROLE_RTL));
    base.face = 0; base.rtl = (font_id & FONT_ROLE_RTL) != 0;
    switch (role) {
    case FONT_ROLE_BODY: base.weight = 400; break;
    case FONT_ROLE_BOLD: case FONT_ROLE_TITLE: base.weight = 700; break;
    case FONT_ROLE_HEADLINE: base.weight = 800; break;
    default: base.face = UINT16_MAX; break;
    }
    return base;
}
