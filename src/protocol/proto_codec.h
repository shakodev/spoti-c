#ifndef SPOTIC_PROTOCOL_PROTO_CODEC_H
#define SPOTIC_PROTOCOL_PROTO_CODEC_H

#include "core/arena.h"
#include "pb.h"

#if defined(PB_ENABLE_MALLOC)
#error Dynamic protobuf allocation is forbidden
#endif

enum { PROTO_MAX_DEPTH = 8, PROTO_MAX_ITEMS = 100, PROTO_MAX_TEXT = 256,
       PROTO_MAX_BLOB = 4096, PROTO_MAX_WIRE = 1024 * 1024 };

typedef enum ProtoResult {
    PROTO_OK, PROTO_INVALID_ARGUMENT, PROTO_MALFORMED, PROTO_LIMIT,
    PROTO_NO_MEMORY
} ProtoResult;

/* Descriptor-aware preflight also bounds discarded callback submessages.
 * Root depth is one. Repeated fields (including packed values/maps) cap at 100.
 * Strings cap at 4095 wire bytes, blobs at 4096; static options may cap lower.
 * Unknown fields are skipped according to their wire type, never as messages.
 * Known text must be UTF-8 without embedded NUL, suitable for C strings.
 * No allocation. Decode destinations must be initialized with *_init_zero when
 * callbacks are used. On failure discard the destination; it may be partial.
 * Callback code must obey the same ownership/budget and must not allocate heap.
 */
ProtoResult proto_validate(const pb_msgdesc_t *desc, const void *data, size_t size);
ProtoResult proto_decode(const pb_msgdesc_t *desc, const void *data, size_t size, void *message);
ProtoResult proto_encode(const pb_msgdesc_t *desc, const void *message,
                         void *data, size_t capacity, size_t *written);

typedef struct ProtoArtist {
    struct ProtoArtist *next;
    uint8_t gid[16];
    bool has_gid;
    char *name;
} ProtoArtist;

typedef struct ProtoAudioFile {
    struct ProtoAudioFile *next;
    uint8_t id[20];
    bool has_id;
    bool has_format;
    int32_t format;
} ProtoAudioFile;

typedef struct ProtoTrack {
    uint8_t gid[16];
    bool has_gid;
    char *name;
    char *uri;
    char *album_name;
    ProtoArtist *artists;
    ProtoAudioFile *files;
    size_t artist_count;
    size_t file_count;
    int32_t duration_ms;
    bool explicit_content;
} ProtoTrack;

/* Selected Track/Album/Artist/AudioFile fields only; caller owns the arena and
 * all returned strings/nodes. Names/URIs <=256 bytes; GIDs/file IDs exactly
 * 16/20 bytes. Arena use rolls back and output remains unchanged on failure.
 * Success references are valid until caller rewinds/resets/disposes the arena.
 * Do not overlap output or input with free arena storage. */
ProtoResult proto_decode_track(const void *data, size_t size, Arena *arena, ProtoTrack *out);

#endif
