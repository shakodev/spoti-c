#ifndef SPOTIC_PLAYER_TEST_H
#define SPOTIC_PLAYER_TEST_H
#include "audio/player.h"
#include "support/test_check.h"
#include <math.h>
#include <string.h>
typedef struct PtFixture { unsigned char ogg[100000]; size_t bytes, frames; float golden[900000]; } PtFixture;
typedef struct PtReader { uint64_t ticket; unsigned fixture; bool used, blocked; unsigned reads; } PtReader;
typedef struct PlayerTest {
    Player *player;
    AudioSink sink;
    Arena core, codec[2], work[2];
    AudioFormat format;
    PtReader readers[2];
    PtFixture files[3];
    uint64_t output_frames;
    uint32_t latency;
    uint32_t source_duration_ms;
    unsigned starts, stops, closes, resolved, retired, peak_readers;
    bool hold_network, hold_retire, fail_prefetch, clock_valid;
    PlayerRequest held[16]; size_t held_count;
    float output[8192];
} PlayerTest;
extern PlayerTest pt;
void pt_load_fixtures(void);
void pt_open(void);
void pt_command(PlayerCommand);
void pt_track(PlayerCommandType, unsigned track, bool play);
void pt_network(void);
void pt_drive(void);
void pt_render(uint32_t frames);
void pt_close(void);
void pt_reply(const PlayerRequest *);
void pt_compare(const float *, const float *, size_t);
void pt_heap_begin(void);
void pt_heap_end(void);
#endif
