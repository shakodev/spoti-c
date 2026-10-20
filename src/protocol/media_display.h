#ifndef SPOTIC_MEDIA_DISPLAY_H
#define SPOTIC_MEDIA_DISPLAY_H
#include <stdint.h>
#include <stdbool.h>
typedef struct MediaDisplay {
    char title[192], artist[96];
    uint8_t cover_id[20];
    bool has_cover;
} MediaDisplay;
#endif
