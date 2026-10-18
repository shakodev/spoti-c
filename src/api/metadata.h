#ifndef SPOTIC_API_METADATA_H
#define SPOTIC_API_METADATA_H

#include "protocol/media_source.h"

enum { METADATA_MAX_RESPONSE = 65536, METADATA_MAX_ALTERNATIVES = 8,
       METADATA_MAX_TRACKS = 16, METADATA_MAX_TRACK_DEPTH = 4 };

typedef struct MetadataAlternatives {
    uint8_t gids[METADATA_MAX_ALTERNATIVES][16];
    size_t count;
} MetadataAlternatives;

/* One TRACK_V4 query; caller supplies bounded storage. No allocation. */
MediaResult metadata_request_body(const char *uri, void *output, size_t capacity,
                                  size_t *written);

/* Validates the complete envelope and every alternative before selecting the
 * highest quality supported Ogg file. No allocation or retained input pointers.
 * Output is unchanged on failure. utc_ms is Unix UTC milliseconds. */
MediaResult metadata_decode(const void *data, size_t bytes, const char *original_uri,
                            const MediaPolicy *policy, MediaTrack *output);

/* As above, additionally returns unblocked, incomplete alternative records that
 * need their own metadata request. alternatives is committed only on OK,
 * RESTRICTED or UNSUPPORTED. The caller bounds requests/visited IDs across
 * responses. Each subsequent envelope must match that request's URI. */
MediaResult metadata_decode_relink(const void *data, size_t bytes, const char *request_uri,
                                   const MediaPolicy *policy, MediaTrack *output,
                                   MetadataAlternatives *alternatives);

#endif
