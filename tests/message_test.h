#ifndef SPOTIC_MESSAGE_TEST_H
#define SPOTIC_MESSAGE_TEST_H
#include "ui/app.h"
#include "support/test_check.h"
#include <string.h>

#define HOME_NOTICE "Personalized Home is unavailable. Showing your playlists."
#define RTL_NOTICE "\xd8\xa7\xd9\x84\xd8\xae\xd8\xaf\xd9\x85\xd8\xa9 \xd8\xba\xd9\x8a\xd8\xb1 \xd9\x85\xd8\xaa\xd8\xa7\xd8\xad\xd8\xa9. 123"
static Clay_String message_string(const char *s) { return (Clay_String){false,(int32_t)strlen(s),s}; }
#endif
