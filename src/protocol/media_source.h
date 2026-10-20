#ifndef SPOTIC_MEDIA_SOURCE_H
#define SPOTIC_MEDIA_SOURCE_H
#include "audio_key.h"
#include "api/token_manager.h"
#include "media_display.h"

enum { MEDIA_SOURCE_SLOTS = 4, MEDIA_URLS = 4, MEDIA_URI_BYTES = 37,
    MEDIA_URL_BYTES = 2048, MEDIA_RESPONSE_BYTES = 65536 };
typedef enum MediaResult { MEDIA_OK, MEDIA_PENDING, MEDIA_INVALID, MEDIA_FULL,
    MEDIA_STALE, MEDIA_CLOSED, MEDIA_CANCELLED, MEDIA_TIMEOUT, MEDIA_MALFORMED,
    MEDIA_LIMIT, MEDIA_RESTRICTED, MEDIA_UNSUPPORTED, MEDIA_HTTP_ERROR, MEDIA_AUTH,
    MEDIA_EXPIRED, MEDIA_STORAGE, MEDIA_KEY_ERROR, MEDIA_EXHAUSTED, MEDIA_BAD_STATE } MediaResult;
typedef struct MediaPolicy {
    char country[3];
    const char *catalogue;
    bool filter_explicit;
    uint64_t utc_ms;
} MediaPolicy;
typedef struct MediaTrack {
    uint8_t gid[16], file_id[20];
    int32_t format;
    uint32_t duration_ms;
    MediaDisplay display;
} MediaTrack;
typedef struct MediaId { uint64_t generation; uint32_t slot, reserved; } MediaId;
typedef MediaId MediaKeyHandle;
_Static_assert(sizeof(MediaKeyHandle) == 16, "16-byte generation-safe key handle");
typedef struct MediaUrl { char url[MEDIA_URL_BYTES]; uint64_t refresh_at; } MediaUrl;
typedef struct MediaSource {
    char original_uri[MEDIA_URI_BYTES];
    MediaTrack track;
    MediaKeyHandle key;
    MediaUrl urls[MEDIA_URLS];
    size_t url_count;
} MediaSource;
typedef struct MediaKeyLease { MediaKeyHandle handle; const uint8_t *key; uint64_t ticket; } MediaKeyLease;
typedef struct MediaResolver MediaResolver;
typedef struct MediaConfig {
    HttpClient *http;
    TokenManager *tokens;
    AudioKeyClient *keys;
    const wchar_t *spclient_origin; /* Exact trusted HTTPS origin from AP resolution. */
    CoreClock clock;
    uint64_t (*utc_ms)(void *);
    void *utc_context;
    MediaPolicy policy; /* Catalogue is copied; utc_ms is read per response. */
    uint32_t timeout_ms; /* Zero: 15000 for complete resolution, including admission. */
    /* Explicit fixture override: IPv4 loopback HTTP origin only. With this
     * override tokens may be NULL. NULL http permits explicit offline source
     * injection only, and media_source_resolve rejects admission in that mode.
     * Never redirects auth or fixture traffic. */
    const wchar_t *fixture_origin;
} MediaConfig;
typedef void (*MediaCallback)(void *, MediaId, MediaResult);

MediaResult media_gid_from_base62(const char *, uint8_t output[16]);
MediaResult media_gid_from_uri(const char *, uint8_t output[16]);
MediaResult media_gid_to_uri(const uint8_t gid[16], char output[MEDIA_URI_BYTES]);
MediaResult media_id_from_hex(const char *, size_t bytes, uint8_t *output);
MediaResult media_id_to_hex(const uint8_t *, size_t bytes, char *output, size_t capacity);
/* One network owner. All callbacks/transport dependencies outlive shutdown.
 * Storage is fixed and owned by the supplied arena. No heap/thread/OS handles.
 * Queue owner work after callbacks; mutations cannot reenter this resolver. */
MediaResult media_resolver_create(Arena *, const MediaConfig *, MediaResolver **);
MediaResult media_source_resolve(MediaResolver *, const char *uri, MediaCallback, void *, MediaId *);
MediaResult media_source_get(MediaResolver *, MediaId, MediaSource *);
MediaResult media_source_poll(MediaResolver *);
uint32_t media_source_next_wait_ms(const MediaResolver *);
/* Retirement cancels network admission and work; key bytes remain pinned until
 * every exact lease ticket is returned and HTTP callbacks retire. Worker users
 * acknowledge on the network owner via release, never mutate source state. */
MediaResult media_source_retire(MediaResolver *, MediaId);
MediaResult media_key_acquire(MediaResolver *, MediaKeyHandle, MediaKeyLease *);
MediaResult media_key_release(MediaResolver *, MediaKeyLease *);
MediaResult media_resolver_shutdown(MediaResolver *);
bool media_resolver_balanced(const MediaResolver *);
#endif
