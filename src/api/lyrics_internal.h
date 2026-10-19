#ifndef SPOTIC_LYRICS_INTERNAL_H
#define SPOTIC_LYRICS_INTERNAL_H
#include "lyrics.h"
#include <limits.h>
#include <stdalign.h>
typedef struct LyricsSlot {
    JsonPage page;
    uint64_t serial;
    uint8_t track_id[16];
    bool pending, cancelled, http_active;
    wchar_t url[HTTP_URL_CHARS], header_names[5][32];
    HttpRequestId http_id;
    TokenAuthorization authorization;
} LyricsSlot;
struct LyricsService {
    JsonPagePool *pool;
    LyricsConfig config;
    uint64_t generation, serial;
    uint8_t track_id[16];
    bool bound, closing;
    const LyricsDocument *current;
    LyricsSlot slots[2];
};
LyricsSlot *lyrics_slot(LyricsService *service, LyricsTicket ticket);
LyricsResult lyrics_build_url(const LyricsConfig *config, const LyricsTrack *track,
    uint8_t id[16], wchar_t output[HTTP_URL_CHARS]);
bool lyrics_fixture_origin_valid(const wchar_t *origin);
LyricsResult lyrics_parse(JsonPage *page, const uint8_t track[16], size_t bytes,
    bool missing, const LyricsDocument **output);
LyricsResult lyrics_finish(LyricsService *service, LyricsTicket ticket, LyricsResult result,
    unsigned status, size_t bytes, const LyricsDocument **output);
LyricsResult lyrics_http_start(LyricsService *service, LyricsSlot *slot);
#endif
