#ifndef SPOTIC_CONNECT_INTERNAL_H
#define SPOTIC_CONNECT_INTERNAL_H
#include "connect_state.h"
#include "connect.pb.h"
#include "transfer_state.pb.h"
#include "protocol/proto_codec.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <string.h>

enum { CONNECT_DEDUP = 32, CONNECT_WIRE_CAP = 16384 };
typedef struct ConnectSeen { char sender[257]; uint32_t id; DealerResult result; bool used, volume; } ConnectSeen;
struct ConnectState {
    ConnectConfig config;
    char connection_id[257], active_device_id[257], last_sender[257];
    ConnectSeen seen[CONNECT_DEDUP]; unsigned cursor;
    uint32_t last_id, sequence;
    uint32_t controls;
    uint64_t revision, sent_revision, connection_epoch, sent_epoch;
    int64_t cluster_timestamp;
    bool active, registered, logged_out, closing, pending, dirty, retry;
    HttpRequestId request;
    TokenAuthorization auth;
    unsigned char upload[CONNECT_WIRE_CAP], response[CONNECT_WIRE_CAP];
    wchar_t url[HTTP_URL_CHARS], connection_header[257];
    SRWLOCK snapshot_lock;
    ConnectSnapshot published;
    PlayerQueueSnapshot observed;
};
bool connect_track_uri(const char *, uint8_t[16]);
void connect_track_text(const uint8_t[16], char[37]);
DealerResult connect_transfer(const void *, size_t, PlayerTransfer *);
void connect_publish(ConnectState *);
DealerResult connect_cluster(ConnectState *, const spotify_connectstate_Cluster *);
#endif
