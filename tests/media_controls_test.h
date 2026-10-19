#ifndef MEDIA_CONTROLS_TEST_H
#define MEDIA_CONTROLS_TEST_H
#include "ui/media_controls_winrt.h"
#include "support/test_check.h"
#include <string.h>
typedef struct MediaFake {
    MediaPublication publication;
    PlayerCommand commands[128];
    unsigned submitted, updates, flags, opened, closed;
    atomic_uint wakes;
    MediaResult publish_result, open_result, close_result;
    PlayerResult submit_result;
    bool close_on_submit;
} MediaFake;
extern MediaControls mc;
extern MediaFake mf;
extern MediaMetadata mm;
extern PlayerSnapshot mp;
void mt_open(void);
void mt_publish(uint64_t now);
void mt_close(void);
void mt_events(void);
void mt_app(void);
int mt_native(void);
#endif
