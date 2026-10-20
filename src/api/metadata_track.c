#include "api/metadata_internal.h"
#include "protocol/proto_codec.h"
#include "metadata.pb.h"
#include <limits.h>
#include <string.h>

typedef struct TrackCandidate {
    MediaTrack track;
    bool blocked;
    bool has_file;
    bool has_audio;
    bool filtered;
} TrackCandidate;

typedef struct TrackSet {
    TrackCandidate tracks[METADATA_MAX_TRACKS];
    size_t count;
    const MediaPolicy *policy;
} TrackSet;

static MediaResult audio_file(MetadataSpan span, TrackCandidate *candidate)
{
    MetadataReader r = {span, MEDIA_OK, 0};
    uint32_t tag; pb_wire_type_t wire; uint64_t seen = 0, format = UINT64_MAX;
    uint8_t id[20] = {0};
    while (metadata_next(&r, &tag, &wire)) {
        if (tag == 1) {
            MetadataSpan value;
            if (!metadata_once(&r, &seen, tag) || !metadata_span(&r, wire, &value)) break;
            if (value.bytes != sizeof(id)) return MEDIA_MALFORMED;
            memcpy(id, value.data, sizeof(id));
        } else if (tag == 2) {
            if (!metadata_once(&r, &seen, tag) || !metadata_uint(&r, wire, &format)) break;
            if (format > INT32_MAX) return MEDIA_MALFORMED;
        } else if (!metadata_skip(&r, wire)) break;
    }
    if (r.result != MEDIA_OK) return r.result;
    if (seen != ((UINT64_C(1) << 1) | (UINT64_C(1) << 2))) return MEDIA_MALFORMED;
    if (format <= 2 && (!candidate->has_file || (int32_t)format > candidate->track.format)) {
        candidate->has_file = true; candidate->track.format = (int32_t)format;
        memcpy(candidate->track.file_id, id, sizeof(id));
    }
    return MEDIA_OK;
}

static MediaResult track_node(MetadataSpan span, unsigned depth, TrackSet *set)
{
    if (depth > METADATA_MAX_TRACK_DEPTH || set->count == METADATA_MAX_TRACKS) return MEDIA_LIMIT;
    size_t index = set->count++;
    TrackCandidate *candidate = &set->tracks[index];
    MediaResult display=metadata_display(span,&candidate->track.display);
    if (display!=MEDIA_OK) { return display; }
    MetadataSpan alternatives[METADATA_MAX_ALTERNATIVES]; size_t alternate_count = 0;
    MetadataReader r = {span, MEDIA_OK, 0};
    uint32_t tag; pb_wire_type_t wire; uint64_t seen = 0;
    bool any_availability = false; unsigned availability_count = 0;
    while (metadata_next(&r, &tag, &wire)) {
        MetadataSpan value; uint64_t scalar;
        switch (tag) {
        case spotify_metadata_Track_gid_tag:
            if (!metadata_once(&r, &seen, tag) || !metadata_span(&r, wire, &value)) break;
            if (value.bytes != sizeof(candidate->track.gid)) return MEDIA_MALFORMED;
            memcpy(candidate->track.gid, value.data, value.bytes);
            break;
        case spotify_metadata_Track_duration_tag:
            if (!metadata_once(&r, &seen, tag) || !metadata_uint(&r, wire, &scalar)) break;
            if (scalar > (uint64_t)INT32_MAX * 2 || (scalar & 1) != 0) return MEDIA_MALFORMED;
            candidate->track.duration_ms = (uint32_t)(scalar / 2);
            break;
        case spotify_metadata_Track_explicit_tag:
            if (!metadata_once(&r, &seen, tag) || !metadata_uint(&r, wire, &scalar)) break;
            if (scalar > 1) return MEDIA_MALFORMED;
            if (scalar == 1 && set->policy->filter_explicit) {
                candidate->blocked = true; candidate->filtered = true;
            }
            break;
        case spotify_metadata_Track_earliest_live_timestamp_tag:
            if (!metadata_once(&r, &seen, tag) || !metadata_uint(&r, wire, &scalar)) break;
            if (scalar > INT64_MAX) return MEDIA_MALFORMED;
            if (scalar > set->policy->utc_ms) candidate->blocked = true;
            break;
        case spotify_metadata_Track_restriction_tag: {
            bool blocked = false;
            if (!metadata_span(&r, wire, &value)) break;
            MediaResult result = metadata_restriction(value, set->policy, &blocked);
            if (result == MEDIA_RESTRICTED) candidate->blocked = true;
            else if (result != MEDIA_OK) return result;
            if (blocked) candidate->blocked = true;
            break;
        }
        case spotify_metadata_Track_availability_tag: {
            bool available = false; ++availability_count;
            if (!metadata_span(&r, wire, &value)) break;
            MediaResult result = metadata_availability(value, set->policy, &available);
            if (result == MEDIA_RESTRICTED) candidate->blocked = true;
            else if (result != MEDIA_OK) return result;
            any_availability |= available;
            break;
        }
        case spotify_metadata_Track_file_tag: {
            candidate->has_audio = true;
            if (!metadata_span(&r, wire, &value)) break;
            MediaResult result = audio_file(value, candidate);
            if (result != MEDIA_OK) return result;
            break;
        }
        case spotify_metadata_Track_alternative_tag:
            if (alternate_count == METADATA_MAX_ALTERNATIVES) return MEDIA_LIMIT;
            if (metadata_span(&r, wire, &value)) alternatives[alternate_count++] = value;
            break;
        case spotify_metadata_Track_sale_period_tag:
            /* Sale-period semantics are not supported by this module. */
            candidate->blocked = true;
            if (!metadata_skip(&r, wire)) break;
            break;
        default:
            if (!metadata_skip(&r, wire)) break;
            break;
        }
        if (r.result != MEDIA_OK) break;
    }
    if (r.result != MEDIA_OK) return r.result;
    if ((seen & (UINT64_C(1) << spotify_metadata_Track_gid_tag)) == 0) return MEDIA_MALFORMED;
    for (size_t i = 0; i < index; ++i) {
        if (memcmp(set->tracks[i].track.gid, candidate->track.gid, 16) == 0) return MEDIA_MALFORMED;
    }
    if (availability_count != 0 && !any_availability) candidate->blocked = true;
    for (size_t i = 0; i < alternate_count; ++i) {
        MediaResult result = track_node(alternatives[i], depth + 1, set);
        if (result != MEDIA_OK) return result;
    }
    return MEDIA_OK;
}

MediaResult metadata_select_track(MetadataSpan span, const uint8_t gid[16],
                                   const MediaPolicy *policy, MediaTrack *output,
                                   MetadataAlternatives *alternatives)
{
    ProtoResult validation = proto_validate(spotify_metadata_Track_fields, span.data, span.bytes);
    if (validation != PROTO_OK) return validation == PROTO_LIMIT ? MEDIA_LIMIT : MEDIA_MALFORMED;
    TrackSet set = {0}; set.policy = policy;
    MediaResult result = track_node(span, 1, &set);
    if (result != MEDIA_OK) return result;
    if (memcmp(set.tracks[0].track.gid, gid, 16) != 0) return MEDIA_MALFORMED;
    if (set.tracks[0].filtered) {
        if (alternatives != NULL) *alternatives = (MetadataAlternatives){0};
        return MEDIA_RESTRICTED;
    }
    bool restricted = false;
    MetadataAlternatives incomplete = {0};
    for (size_t i = 0; i < set.count; ++i) {
        const TrackCandidate *candidate = &set.tracks[i];
        restricted |= candidate->blocked;
        if (!candidate->blocked && candidate->has_file && candidate->track.duration_ms != 0) {
            *output = candidate->track;
            if (alternatives != NULL) *alternatives = (MetadataAlternatives){0};
            return MEDIA_OK;
        }
    }
    if (alternatives != NULL) {
        for (size_t i = 1; i < set.count; ++i) {
            const TrackCandidate *candidate = &set.tracks[i];
            if (candidate->blocked || (candidate->has_audio && candidate->track.duration_ms != 0)) continue;
            if (incomplete.count == METADATA_MAX_ALTERNATIVES) return MEDIA_LIMIT;
            memcpy(incomplete.gids[incomplete.count++], candidate->track.gid, 16);
        }
        *alternatives = incomplete;
    }
    return restricted ? MEDIA_RESTRICTED : MEDIA_UNSUPPORTED;
}
