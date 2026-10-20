#include "api/metadata_internal.h"
#include "extended_metadata.pb.h"
#include "pb_common.h"
#include "pb_encode.h"
#include <limits.h>
#include <string.h>

static bool encode_query(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
    (void)arg;
    spotify_extendedmetadata_ExtensionQuery query = spotify_extendedmetadata_ExtensionQuery_init_zero;
    query.extension_kind = spotify_extendedmetadata_ExtensionKind_TRACK_V4;
    return pb_encode_tag_for_field(stream, field) &&
        pb_encode_submessage(stream, spotify_extendedmetadata_ExtensionQuery_fields, &query);
}

static bool encode_entity(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
    const char *uri = *arg;
    spotify_extendedmetadata_EntityRequest entity = spotify_extendedmetadata_EntityRequest_init_zero;
    memcpy(entity.entity_uri, uri, strlen(uri) + 1);
    entity.query.funcs.encode = encode_query;
    return pb_encode_tag_for_field(stream, field) &&
        pb_encode_submessage(stream, spotify_extendedmetadata_EntityRequest_fields, &entity);
}

MediaResult metadata_request_body(const char *uri, void *output, size_t capacity, size_t *written)
{
    uint8_t gid[16], buffer[128];
    if (written != NULL) *written = 0;
    if (output == NULL || written == NULL || media_gid_from_uri(uri, gid) != MEDIA_OK) return MEDIA_INVALID;
    spotify_extendedmetadata_BatchedEntityRequest request = spotify_extendedmetadata_BatchedEntityRequest_init_zero;
    request.entity_request.funcs.encode = encode_entity;
    request.entity_request.arg = (void *)uri;
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    if (!pb_encode(&stream, spotify_extendedmetadata_BatchedEntityRequest_fields, &request)) return MEDIA_LIMIT;
    if (stream.bytes_written > capacity) return MEDIA_FULL;
    memcpy(output, buffer, stream.bytes_written); *written = stream.bytes_written;
    return MEDIA_OK;
}

static MediaResult empty_header(MetadataSpan span)
{
    MetadataReader r = {span, MEDIA_OK, 0}; uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r, &tag, &wire)) {
        (void)tag;
        if (!metadata_skip(&r, wire)) break;
    }
    return r.result;
}

static MediaResult array_header(MetadataSpan span)
{
    MetadataReader r = {span, MEDIA_OK, 0}; uint64_t seen = 0;
    uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r, &tag, &wire)) {
        if (tag <= 4) {
            uint64_t value;
            if (!metadata_once(&r, &seen, tag) || !metadata_uint(&r, wire, &value)) break;
            if (value > INT64_MAX || (tag == 1 && value > INT32_MAX)) return MEDIA_MALFORMED;
            if (tag == 1 && value != 0 && value != 200) return MEDIA_HTTP_ERROR;
            if (tag == 4 && value > 1) return MEDIA_MALFORMED;
        } else if (!metadata_skip(&r, wire)) break;
    }
    return r.result;
}

static MediaResult entity_header(MetadataSpan span)
{
    MetadataReader r = {span, MEDIA_OK, 0}; uint64_t seen = 0, status = 0;
    uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r, &tag, &wire)) {
        if (tag >= 1 && tag <= 5) {
            if (!metadata_once(&r, &seen, tag)) break;
            if (tag == 2 || tag == 3) {
                MetadataSpan text;
                if (!metadata_span(&r, wire, &text)) break;
                if (text.bytes > 256) return MEDIA_LIMIT;
                char copy[257]; memcpy(copy, text.data, text.bytes); copy[text.bytes] = '\0';
                if (memchr(copy, 0, text.bytes) != NULL || !pb_validate_utf8(copy)) return MEDIA_MALFORMED;
            } else {
                uint64_t value;
                if (!metadata_uint(&r, wire, &value)) break;
                if (value > INT64_MAX || (tag == 1 && value > INT32_MAX)) return MEDIA_MALFORMED;
                if (tag == 1) status = value;
            }
        } else if (!metadata_skip(&r, wire)) break;
    }
    if (r.result != MEDIA_OK) return r.result;
    if ((seen & (UINT64_C(1) << 1)) == 0) return MEDIA_MALFORMED;
    return status == 200 ? MEDIA_OK : MEDIA_HTTP_ERROR;
}

static MediaResult any_track(MetadataSpan span, MetadataSpan *track)
{
    MetadataReader r = {span, MEDIA_OK, 0}; uint64_t seen = 0;
    uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r, &tag, &wire)) {
        if (tag == 1 || tag == 2) {
            MetadataSpan value;
            if (!metadata_once(&r, &seen, tag) || !metadata_span(&r, wire, &value)) break;
            if (tag == 1) {
                if (!metadata_equal(value, "type.googleapis.com/spotify.metadata.Track")) return MEDIA_MALFORMED;
            } else *track = value;
        } else if (!metadata_skip(&r, wire)) break;
    }
    if (r.result != MEDIA_OK) return r.result;
    return seen == 6 && track->bytes != 0 ? MEDIA_OK : MEDIA_MALFORMED;
}

static MediaResult entity_data(MetadataSpan span, const char *uri, MetadataSpan *track)
{
    MetadataReader r = {span, MEDIA_OK, 0}; uint64_t seen = 0;
    uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r, &tag, &wire)) {
        if (tag >= 1 && tag <= 3) {
            MetadataSpan value;
            if (!metadata_once(&r, &seen, tag) || !metadata_span(&r, wire, &value)) break;
            MediaResult result = MEDIA_OK;
            if (tag == 1) result = entity_header(value);
            else if (tag == 2 && !metadata_equal(value, uri)) result = MEDIA_MALFORMED;
            else if (tag == 3) result = any_track(value, track);
            if (result != MEDIA_OK) return result;
        } else if (!metadata_skip(&r, wire)) break;
    }
    if (r.result != MEDIA_OK) return r.result;
    return seen == 14 ? MEDIA_OK : MEDIA_MALFORMED;
}

static MediaResult extension_array(MetadataSpan span, const char *uri, MetadataSpan *track)
{
    MetadataReader r = {span, MEDIA_OK, 0}; uint64_t seen = 0;
    uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r, &tag, &wire)) {
        if (tag >= 1 && tag <= 3) {
            if (!metadata_once(&r, &seen, tag)) break;
            if (tag == 2) {
                uint64_t value;
                if (!metadata_uint(&r, wire, &value)) break;
                if (value != spotify_extendedmetadata_ExtensionKind_TRACK_V4) return MEDIA_MALFORMED;
            } else {
                MetadataSpan value;
                if (!metadata_span(&r, wire, &value)) break;
                MediaResult result = tag == 1 ? array_header(value) : entity_data(value, uri, track);
                if (result != MEDIA_OK) return result;
            }
        } else if (!metadata_skip(&r, wire)) break;
    }
    if (r.result != MEDIA_OK) return r.result;
    return (seen & 12) == 12 ? MEDIA_OK : MEDIA_MALFORMED;
}

MediaResult metadata_decode_relink(const void *data, size_t bytes, const char *uri,
                                   const MediaPolicy *policy, MediaTrack *output,
                                   MetadataAlternatives *alternatives)
{
    uint8_t gid[16];
    if (data == NULL || policy == NULL || output == NULL ||
        media_gid_from_uri(uri, gid) != MEDIA_OK || policy->catalogue == NULL ||
        policy->catalogue[0] == '\0' || strlen(policy->catalogue) > 64 ||
        policy->country[0] < 'A' || policy->country[0] > 'Z' ||
        policy->country[1] < 'A' || policy->country[1] > 'Z' || policy->country[2] != '\0') return MEDIA_INVALID;
    if (bytes > METADATA_MAX_RESPONSE) return MEDIA_LIMIT;
    MetadataReader r = {{data, bytes}, MEDIA_OK, 0}; uint64_t seen = 0;
    MetadataSpan track = {0}; uint32_t tag; pb_wire_type_t wire;
    while (metadata_next(&r, &tag, &wire)) {
        if (tag == 1 || tag == 2) {
            MetadataSpan value;
            if (!metadata_once(&r, &seen, tag) || !metadata_span(&r, wire, &value)) break;
            MediaResult result = tag == 1 ? empty_header(value) : extension_array(value, uri, &track);
            if (result != MEDIA_OK) return result;
        } else if (!metadata_skip(&r, wire)) break;
    }
    if (r.result != MEDIA_OK) return r.result;
    if ((seen & 4) == 0) return MEDIA_MALFORMED;
    return metadata_select_track(track, gid, policy, output, alternatives);
}

MediaResult metadata_decode(const void *data, size_t bytes, const char *uri,
                            const MediaPolicy *policy, MediaTrack *output)
{
    return metadata_decode_relink(data, bytes, uri, policy, output, NULL);
}
