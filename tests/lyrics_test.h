#ifndef SPOTIC_LYRICS_TEST_H
#define SPOTIC_LYRICS_TEST_H
#include "api/lyrics.h"
#include "support/test_check.h"
#include <string.h>
#define LYRICS_BODY(mode,cap,rows,previews) "{\"lyrics\":{\"syncType\":\"" mode "\",\"capStatus\":\"" cap "\",\"provider\":\"fixture\",\"providerDisplayName\":\"Fixture\",\"providerLyricsId\":\"id\",\"language\":\"multi\",\"lines\":" rows ",\"previewLines\":" previews "}}"
extern LyricsService *lt_service;
extern JsonPagePool *lt_pool;
static inline JsonString lyric_span(const char *s) { return (JsonString){s,strlen(s)}; }
LyricsRequest lt_request(uint64_t generation, unsigned track);
LyricsResult lt_deliver(LyricsRequest request, const char *json, const LyricsDocument **output);
LyricsResult lt_fixture(LyricsRequest request, const char *name, const LyricsDocument **output);
void lyrics_error_tests(void);
void lyrics_timeline_tests(void);
void lyrics_http_tests(void);
void lyrics_lifetime_tests(void);
#endif
