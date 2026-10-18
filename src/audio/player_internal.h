#ifndef SPOTIC_PLAYER_INTERNAL_H
#define SPOTIC_PLAYER_INTERNAL_H
#include "player.h"
#include <string.h>
typedef enum PlayerSlotState { PS_FREE, PS_REQUESTED, PS_READY, PS_FAILED, PS_RETIRING } PlayerSlotState;
typedef struct PlayerSlot {
    PlayerSlotState state;
    uint64_t ticket, decoder_generation, target;
    unsigned index;
    PlayerSource source;
    VorbisDecoder *decoder;
    size_t frames, offset;
    bool targeted, eof, retirement_sent;
    AudioResult failure;
    float block[PLAYER_BLOCK_FRAMES * 2];
} PlayerSlot;
typedef struct PlayerMarker {
    uint64_t media, position, generation;
    unsigned index;
} PlayerMarker;
struct Player {
    SRWLOCK lock;
    HANDLE wake, requests_ready;
    PlayerCommand commands[PLAYER_COMMAND_CAP];
    PlayerRequest requests[PLAYER_REQUEST_CAP];
    size_t command_head, command_count, request_head, request_count;
    PlayerSnapshot published, view;
    PlayerConfig config;
    DWORD owner;
    bool control_closed, destroyed, busy, closing, want_play, desired, final_eof, more;
    uint64_t serial, epoch, written, desired_position;
    unsigned desired_index, producer_index;
    int current, next;
    PlayerSlot slots[2];
    uint64_t live_ticket[2]; /* Protected by lock for reply admission. */
    bool retiring[2];
    unsigned queue_admitted;
    uint8_t ids[PLAYER_QUEUE_CAP][16];
    unsigned order[PLAYER_QUEUE_CAP], queue_count;
    PlayerRepeat repeat;
    AudioResult pending_error;
    uint32_t shuffle;
    PlayerMarker markers[PLAYER_MARKERS];
    size_t marker_head, marker_count;
};
_Static_assert(sizeof(Player) <= 32 * MEMORY_KIB, "bounded player core metadata");
bool player_owner(const Player *);
bool player_id_valid(const uint8_t *);
void player_publish(Player *);
bool player_pop_command(Player *, PlayerCommand *);
bool player_push_request(Player *, const PlayerRequest *);
void player_ticket_publish(Player *, unsigned);
bool player_next_index(Player *, unsigned index, bool automatic, unsigned *);
void player_queue_shuffle(Player *, uint32_t seed);
void player_retire_slot(Player *, unsigned);
void player_retire_all(Player *);
void player_sources_drive(Player *);
AudioResult player_slot_read(Player *, unsigned);
void player_fail(Player *, AudioResult);
void player_begin(Player *, unsigned index, uint64_t position, PlayerState state);
void player_timeline(Player *);
bool player_marker_add(Player *, unsigned index, uint64_t position);
void player_decode_drive(Player *);
void player_command(Player *, const PlayerCommand *);
#endif
