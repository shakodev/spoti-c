#ifndef SPOTIC_STORAGE_RESOLVE_H
#define SPOTIC_STORAGE_RESOLVE_H
#include "protocol/media_source.h"
/* Transactional parse, checks an echoed file ID when present. Deadlines are
 * monotonic; utc_ms is only used to translate signed URL timestamps. Unknown
 * expiry formats receive a finite 60-second lease, never an infinite cache. */
MediaResult storage_resolve_decode(const void *, size_t, const uint8_t file_id[20],
    uint64_t utc_ms, uint64_t now_ms, MediaUrl output[MEDIA_URLS], size_t *count);
/* Endpoint construction is implemented only after the native REA request gate. */
MediaResult storage_resolve_url(const wchar_t *origin, const MediaTrack *,
    wchar_t output[HTTP_URL_CHARS]);
#endif
