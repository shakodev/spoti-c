#ifndef SPOTIC_MEDIA_CONTROLS_H
#define SPOTIC_MEDIA_CONTROLS_H
#include "audio/player.h"
#define MEDIA_INPUT_CAP 16u
#define MEDIA_DUPLICATE_MS 200u
typedef enum MediaResult { MEDIA_OK, MEDIA_FULL, MEDIA_INVALID, MEDIA_CLOSED,
    MEDIA_STALE, MEDIA_UNSUPPORTED, MEDIA_PLATFORM_ERROR, MEDIA_LIMIT } MediaResult;
typedef enum MediaAction { MEDIA_PLAY, MEDIA_PAUSE, MEDIA_TOGGLE, MEDIA_STOP,
    MEDIA_NEXT, MEDIA_PREVIOUS, MEDIA_SEEK } MediaAction;
typedef enum MediaSource { MEDIA_SYSTEM, MEDIA_WINDOW, MEDIA_KEY } MediaSource;
typedef enum MediaStatus { MEDIA_STATUS_CLOSED, MEDIA_STATUS_CHANGING, MEDIA_STATUS_STOPPED,
    MEDIA_STATUS_PLAYING, MEDIA_STATUS_PAUSED } MediaStatus;
enum MediaCapability { MEDIA_CAN_PLAY=1, MEDIA_CAN_PAUSE=2, MEDIA_CAN_STOP=4,
    MEDIA_CAN_NEXT=8, MEDIA_CAN_PREVIOUS=16, MEDIA_CAN_SEEK=32 };
enum MediaChange { MEDIA_METADATA=1, MEDIA_STATE=2, MEDIA_TIMELINE=4 };
typedef struct MediaMetadata { uint8_t track_id[16]; char title[192], artist[192], artwork[512]; } MediaMetadata;
typedef struct MediaPublication {
    MediaMetadata metadata;
    PlayerSnapshot player;
    uint64_t position, duration; /* Checked signed-TimeSpan-compatible 100 ns. */
    uint32_t capabilities;
    MediaStatus status;
} MediaPublication;
typedef struct MediaInput {
    MediaAction action; MediaSource source;
    uint64_t now, generation; int64_t position;
    uint8_t track_id[16];
} MediaInput;
struct MediaControls;
/* Adapter functions run only on the initialization owner, outside frames.
 * open unwinds its own partial state. close detaches all callback access before
 * returning; independently retained delegate cells may finish afterwards. */
typedef struct MediaAdapter {
    MediaResult (*open)(void *, struct MediaControls *);
    MediaResult (*publish)(void *, const MediaPublication *, uint32_t changes);
    MediaResult (*close)(void *);
    void *context;
} MediaAdapter;
typedef struct MediaConfig {
    PlayerResult (*submit)(void *, const PlayerCommand *); /* Existing bounded MPSC player admission. */
    void (*wake)(void *); /* Nonblocking signal only; never reenter close/pump. */
    void *context;
} MediaConfig;
typedef struct MediaStats { _Atomic uint64_t queued, submitted, duplicates, stale, full, publications; } MediaStats;
typedef struct MediaControls {
    SRWLOCK lock;
    MediaConfig config; MediaAdapter adapter;
    MediaInput inputs[MEDIA_INPUT_CAP], last;
    MediaPublication published;
    PlayerSnapshot latest;
    uint32_t capabilities, count, head;
    uint64_t published_at;
    DWORD owner;
    MediaStats stats;
    bool alive, closing, has_publication, has_last;
} MediaControls;
/* Caller storage starts fresh, remains live through close and every API call.
 * Only post is MPSC/thread-safe; publish/pump/close belong to the UI owner. */
MediaResult media_controls_init(MediaControls *, MediaConfig, MediaAdapter);
MediaResult media_controls_publish(MediaControls *, const MediaMetadata *, PlayerSnapshot, uint32_t capabilities, uint64_t now);
MediaResult media_controls_post(MediaControls *, MediaAction, MediaSource, int64_t position_100ns, uint64_t now);
MediaResult media_controls_pump(MediaControls *);
MediaResult media_controls_close(MediaControls *);
MediaResult media_controls_appcommand(MediaControls *, unsigned command, uint64_t now);
MediaResult media_controls_key(MediaControls *, unsigned key, bool repeat, uint64_t now);
#endif
