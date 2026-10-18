#ifndef SPOTIC_AUDIO_PLAYER_H
#define SPOTIC_AUDIO_PLAYER_H
#include "audio_sink.h"
#include "vorbis_decoder.h"

enum { PLAYER_QUEUE_CAP = 64, PLAYER_COMMAND_CAP = 32, PLAYER_REQUEST_CAP = 16,
    PLAYER_MARKERS = 64, PLAYER_BLOCK_FRAMES = 256 };
typedef enum PlayerState { PLAYER_STOPPED, PLAYER_LOADING, PLAYER_BUFFERING, PLAYER_PLAYING,
    PLAYER_PAUSED, PLAYER_SEEKING, PLAYER_ENDED, PLAYER_ERROR } PlayerState;
typedef enum PlayerResult { PLAYER_OK, PLAYER_MORE, PLAYER_FULL, PLAYER_CLOSED,
    PLAYER_INVALID, PLAYER_STALE, PLAYER_BAD_STATE, PLAYER_LIMIT, PLAYER_NO_MEMORY } PlayerResult;
typedef enum PlayerRepeat { PLAYER_REPEAT_OFF, PLAYER_REPEAT_ONE, PLAYER_REPEAT_ALL } PlayerRepeat;
typedef struct PlayerSnapshot {
    uint64_t generation;
    uint8_t track_id[16];
    uint64_t position_frames;
    uint32_t rate;
    float volume;
    PlayerState state;
    AudioResult error;
    bool timeline_valid;
} PlayerSnapshot;
/* Network-owner handoff. Context remains live until its RETIRE acknowledgement.
 * read_at executes only on the decode owner and must copy already prepared
 * logical decrypted Vorbis bytes or return NEED_MORE. It receives the source
 * ticket (not the public generation). Never call network-owner APIs here.
 * After preparing data, submit DATA_READY with that ticket to wake the owner. */
typedef struct PlayerSource { uint64_t bytes; DecoderReadAt read_at; void *context; } PlayerSource;
typedef enum PlayerCommandType {
    PLAYER_LOAD, PLAYER_PLAY, PLAYER_PAUSE, PLAYER_STOP, PLAYER_SEEK, PLAYER_VOLUME,
    PLAYER_QUEUE_APPEND, PLAYER_QUEUE_CLEAR, PLAYER_NEXT, PLAYER_PREVIOUS,
    PLAYER_REPEAT, PLAYER_SHUFFLE, PLAYER_SHUTDOWN,
    PLAYER_SOURCE_READY, PLAYER_SOURCE_FAILED, PLAYER_SOURCE_RETIRED, PLAYER_DATA_READY
} PlayerCommandType;
typedef struct PlayerCommand {
    PlayerCommandType type;
    uint8_t track_id[16];
    uint64_t ticket, position_frames;
    float volume;
    uint32_t value; /* repeat enum or shuffle seed (0 disables) */
    bool play; /* LOAD starts playing when true, otherwise prepares paused. */
    PlayerSource source;
    AudioResult error;
} PlayerCommand;
typedef enum PlayerRequestType { PLAYER_RESOLVE, PLAYER_RETIRE } PlayerRequestType;
typedef struct PlayerRequest {
    PlayerRequestType type;
    uint8_t track_id[16];
    uint64_t ticket, generation, position_frames;
    AudioFormat format;
    bool prefetch;
} PlayerRequest;
typedef struct Player Player;
typedef struct PlayerConfig {
    AudioSink *sink; /* Already opened on this same non-GUI decode/device owner. */
    Arena *codec[2], *work[2]; /* Empty, distinct, fixed 256 KiB / 64 KiB arenas. */
} PlayerConfig;
/* Core allocation is fixed, counted under its existing <=128 KiB cap. No heap
 * or new thread. All APIs except submit/snapshot/take_request are owner-only.
 * Decoder and device ownership share a worker; network remains separate. */
PlayerResult player_create(Arena *core, const PlayerConfig *, Player **);
/* MPSC UI/Connect/SMTC commands AND network replies; FULL retains caller data.
 * Ticket-bearing replies are checked on admission and again on consumption.
 * A successful SHUTDOWN closes control admission but allows retire replies. */
PlayerResult player_submit(Player *, const PlayerCommand *);
PlayerSnapshot player_snapshot(Player *); /* Immutable copy, short publication lock. */
/* One network owner consumes requests and maps IDs to existing media/range/
 * decrypt modules. RETIRE must cancel HTTP generations and finish leases,
 * then submit SOURCE_RETIRED. READY/FAILED replies carry the exact ticket.
 * No slot reuse before acknowledgement; stale READY retains network ownership. */
PlayerResult player_take_request(Player *, PlayerRequest *);
HANDLE player_request_event(Player *); /* Borrowed, manual reset; do not reset. */
PlayerResult player_pump(Player *); /* Bounded work; MORE means immediately drive again. */
size_t player_wait_handles(Player *, HANDLE output[3]);
/* Wait timeout, not a clock: <=1 Hz while playing or a nearer audible boundary.
 * Positions always come from the device timeline. Paused/stopped waits infinite. */
DWORD player_next_wait_ms(Player *);
/* Submit shutdown, quiesce external command producers, drive network retire
 * replies until pump returns CLOSED, then join network and destroy. */
PlayerResult player_destroy(Player *);
#endif
