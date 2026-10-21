#ifndef SPOTIC_LIVE_WIRE_H
#define SPOTIC_LIVE_WIRE_H
#include "api/metadata_internal.h"
#define LIVE_ROWS 64u
typedef struct LiveRow { char uri[192], name[192]; } LiveRow;
typedef struct LiveList {
    LiveRow rows[LIVE_ROWS];
    uint32_t count, total, position;
    char name[192], revision[65];
    bool contents, truncated;
} LiveList;
bool live_list_decode(const void *, size_t, LiveList *);
bool live_url_component(const char *, wchar_t *, size_t);
#endif
