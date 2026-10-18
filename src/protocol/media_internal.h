#ifndef SPOTIC_MEDIA_INTERNAL_H
#define SPOTIC_MEDIA_INTERNAL_H
#include "media_source.h"
#include "api/metadata.h"
#include "api/storage_resolve.h"
#include "core/memory_budget.h"
#include <string.h>
enum MediaStage { MS_FREE, MS_METADATA, MS_KEY, MS_STORAGE, MS_READY, MS_FAILED, MS_RETIRED };
enum { MEDIA_LEASES = 16 };
typedef struct MediaCandidate { uint8_t gid[16]; unsigned depth; } MediaCandidate;
typedef struct MediaSlot {
    MediaSource source;
    MediaId id;
    MediaResult result;
    enum MediaStage stage;
    uint64_t deadline;
    uint8_t secret[16];
    bool notified;
    AudioKeyId key_request;
    MediaCallback callback;
    void *context;
    size_t leases;
    TokenAuthorization authorization;
    char request_uri[MEDIA_URI_BYTES];
    MediaCandidate candidates[16];
    size_t candidate, candidate_count;
} MediaSlot;
typedef struct MediaTicket { uint64_t ticket; MediaId handle; } MediaTicket;
struct MediaResolver {
    MediaConfig config;
    wchar_t origin[256], url[HTTP_URL_CHARS];
    char catalogue[33];
    MediaSlot slots[MEDIA_SOURCE_SLOTS];
    MediaTicket leases[MEDIA_LEASES];
    uint64_t generation, ticket;
    bool busy, shutdown, needs_drive;
    int active_slot;
    HttpRequestId http_id;
    uint8_t response[MEDIA_RESPONSE_BYTES], upload[512];
};
_Static_assert(sizeof(MediaResolver) <= MEMORY_BUDGET_METADATA, "bounded media resolver storage");
MediaSlot *media_slot(MediaResolver *, MediaId);
bool media_same_id(MediaId, MediaId);
void media_reap(MediaResolver *, MediaSlot *);
void media_finish(MediaSlot *, MediaResult);
MediaResult media_http_poll(MediaResolver *);
MediaResult media_http_start(MediaResolver *, size_t);
void media_key_completed(void *, AudioKeyId, ApResult, const uint8_t key[16]);
uint64_t media_utc(const MediaResolver *);
#endif
