#include "protocol/proto_codec.h"
#include "metadata.pb.h"
#include "pb_common.h"
#include "pb_decode.h"

#include <limits.h>
#include <string.h>

typedef struct TrackDecode {
    Arena *arena;
    ProtoResult result;
    ProtoTrack track;
} TrackDecode;

typedef struct ItemDecode {
    TrackDecode *owner;
    void *item;
} ItemDecode;

static bool fail(TrackDecode *ctx, ProtoResult result)
{
    ctx->result = result;
    return false;
}

static bool text(pb_istream_t *stream, TrackDecode *ctx, char **out)
{
    size_t length = stream->bytes_left;
    if (length > PROTO_MAX_TEXT) return fail(ctx, PROTO_LIMIT);
    char *value = arena_alloc(ctx->arena, length + 1, 1);
    if (!value) return fail(ctx, PROTO_NO_MEMORY);
    if (!pb_read(stream, (pb_byte_t *)value, length)) return fail(ctx, PROTO_MALFORMED);
    value[length] = '\0';
    if (memchr(value, 0, length) || !pb_validate_utf8(value)) return fail(ctx, PROTO_MALFORMED);
    *out = value;
    return true;
}

static bool bytes(pb_istream_t *stream, TrackDecode *ctx, uint8_t *out, size_t count)
{
    if (stream->bytes_left != count) return fail(ctx, PROTO_MALFORMED);
    return pb_read(stream, out, count);
}

static void callback(pb_callback_t *field,
                     bool (*fn)(pb_istream_t *, const pb_field_t *, void **), void *ctx)
{
    field->funcs.decode = fn;
    field->arg = ctx;
}

static bool decode_artist(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    ItemDecode *ctx = *arg;
    ProtoArtist *artist = ctx->item;
    if (field->tag == spotify_metadata_Artist_name_tag)
        return text(stream, ctx->owner, &artist->name);
    artist->has_gid = bytes(stream, ctx->owner, artist->gid, sizeof(artist->gid));
    return artist->has_gid;
}

static bool decode_file(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    ItemDecode *ctx = *arg;
    ProtoAudioFile *file = ctx->item;
    if (field->tag == spotify_metadata_AudioFile_file_id_tag) {
        file->has_id = bytes(stream, ctx->owner, file->id, sizeof(file->id));
        return file->has_id;
    }
    uint64_t value;
    if (!pb_decode_varint(stream, &value) || value > INT32_MAX)
        return fail(ctx->owner, PROTO_MALFORMED);
    file->has_format = true;
    file->format = (int32_t)value;
    return true;
}

static bool decode_album(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    TrackDecode *ctx = *arg;
    (void)field;
    return text(stream, ctx, &ctx->track.album_name);
}

static bool decode_track_field(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    TrackDecode *ctx = *arg;
    ProtoTrack *track = &ctx->track;
    switch (field->tag) {
    case spotify_metadata_Track_gid_tag:
        track->has_gid = bytes(stream, ctx, track->gid, sizeof(track->gid));
        return track->has_gid;
    case spotify_metadata_Track_name_tag: return text(stream, ctx, &track->name);
    case spotify_metadata_Track_canonical_uri_tag: return text(stream, ctx, &track->uri);
    case spotify_metadata_Track_duration_tag: {
        int64_t value;
        if (!pb_decode_svarint(stream, &value) || value < 0 || value > INT32_MAX)
            return fail(ctx, PROTO_MALFORMED);
        track->duration_ms = (int32_t)value;
        return true;
    }
    case spotify_metadata_Track_explicit_tag: {
        uint64_t value;
        if (!pb_decode_varint(stream, &value)) return fail(ctx, PROTO_MALFORMED);
        track->explicit_content = value != 0;
        return true;
    }
    case spotify_metadata_Track_album_tag: {
        spotify_metadata_Album album = spotify_metadata_Album_init_zero;
        callback(&album.name, decode_album, ctx);
        return pb_decode(stream, spotify_metadata_Album_fields, &album);
    }
    case spotify_metadata_Track_artist_tag: {
        if (track->artist_count == PROTO_MAX_ITEMS) return fail(ctx, PROTO_LIMIT);
        ProtoArtist *artist = arena_alloc(ctx->arena, sizeof(*artist), _Alignof(ProtoArtist));
        if (!artist) return fail(ctx, PROTO_NO_MEMORY);
        memset(artist, 0, sizeof(*artist));
        ItemDecode item = {ctx, artist};
        spotify_metadata_Artist wire = spotify_metadata_Artist_init_zero;
        callback(&wire.name, decode_artist, &item);
        callback(&wire.gid, decode_artist, &item);
        if (!pb_decode(stream, spotify_metadata_Artist_fields, &wire)) return false;
        ProtoArtist **tail = &track->artists;
        while (*tail) tail = &(*tail)->next;
        *tail = artist;
        ++track->artist_count;
        return true;
    }
    case spotify_metadata_Track_file_tag: {
        if (track->file_count == PROTO_MAX_ITEMS) return fail(ctx, PROTO_LIMIT);
        ProtoAudioFile *file = arena_alloc(ctx->arena, sizeof(*file), _Alignof(ProtoAudioFile));
        if (!file) return fail(ctx, PROTO_NO_MEMORY);
        memset(file, 0, sizeof(*file));
        ItemDecode item = {ctx, file};
        spotify_metadata_AudioFile wire = spotify_metadata_AudioFile_init_zero;
        callback(&wire.file_id, decode_file, &item);
        callback(&wire.format, decode_file, &item);
        if (!pb_decode(stream, spotify_metadata_AudioFile_fields, &wire)) return false;
        ProtoAudioFile **tail = &track->files;
        while (*tail) tail = &(*tail)->next;
        *tail = file;
        ++track->file_count;
        return true;
    }
    default: return false;
    }
}

ProtoResult proto_decode_track(const void *data, size_t size, Arena *arena, ProtoTrack *out)
{
    if (!arena || !out) return PROTO_INVALID_ARGUMENT;
    ProtoResult result = proto_validate(spotify_metadata_Track_fields, data, size);
    if (result != PROTO_OK) return result;
    ArenaMark mark = arena_mark(arena);
    TrackDecode ctx = {0};
    ctx.arena = arena;
    spotify_metadata_Track wire = spotify_metadata_Track_init_zero;
    callback(&wire.gid, decode_track_field, &ctx);
    callback(&wire.name, decode_track_field, &ctx);
    callback(&wire.canonical_uri, decode_track_field, &ctx);
    callback(&wire.duration, decode_track_field, &ctx);
    callback(&wire.explicit, decode_track_field, &ctx);
    callback(&wire.album, decode_track_field, &ctx);
    callback(&wire.artist, decode_track_field, &ctx);
    callback(&wire.file, decode_track_field, &ctx);
    pb_istream_t stream = pb_istream_from_buffer(data, size);
    if (!pb_decode(&stream, spotify_metadata_Track_fields, &wire)) {
        (void)arena_rewind(arena, mark);
        return ctx.result == PROTO_OK ? PROTO_MALFORMED : ctx.result;
    }
    *out = ctx.track;
    return PROTO_OK;
}
