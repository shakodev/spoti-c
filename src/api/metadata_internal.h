#ifndef SPOTIC_API_METADATA_INTERNAL_H
#define SPOTIC_API_METADATA_INTERNAL_H

#include "api/metadata.h"
#include "pb_decode.h"

typedef struct MetadataSpan { const uint8_t *data; size_t bytes; } MetadataSpan;
typedef struct MetadataReader { MetadataSpan span; MediaResult result; unsigned fields; } MetadataReader;

bool metadata_next(MetadataReader *r, uint32_t *tag, pb_wire_type_t *wire);
bool metadata_uint(MetadataReader *r, pb_wire_type_t wire, uint64_t *value);
bool metadata_span(MetadataReader *r, pb_wire_type_t wire, MetadataSpan *span);
bool metadata_skip(MetadataReader *r, pb_wire_type_t wire);
bool metadata_once(MetadataReader *r, uint64_t *seen, uint32_t tag);
bool metadata_equal(MetadataSpan span, const char *text);
MediaResult metadata_restriction(MetadataSpan span, const MediaPolicy *policy, bool *blocked);
MediaResult metadata_availability(MetadataSpan span, const MediaPolicy *policy, bool *available);
MediaResult metadata_select_track(MetadataSpan span, const uint8_t gid[16],
                                   const MediaPolicy *policy, MediaTrack *output,
                                   MetadataAlternatives *alternatives);
MediaResult metadata_display(MetadataSpan, MediaDisplay *);

#endif
