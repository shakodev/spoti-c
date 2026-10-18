#ifndef SPOTIC_SINK_TEST_H
#define SPOTIC_SINK_TEST_H
#include "audio/audio_sink.h"
#include "support/test_check.h"
#include <string.h>
typedef struct FakeSink {
    AudioSink *sink;
    AudioFormat format;
    SinkClock clock;
    unsigned opens, starts, stops, closes, clocks;
    bool fail_open, fail_start, fail_stop;
    void *allocation;
} FakeSink;
extern const SinkBackend fake_sink_backend;
void sink_race_tests(void);
static inline void fake_init(FakeSink *f)
{
    memset(f, 0, sizeof(*f)); f->format = (AudioFormat){48000,2,AUDIO_FLOAT32};
    f->clock = (SinkClock){12,48000,100000,25000,128,true};
}
#endif
