#ifndef SPOTIC_LYRICS_H
#define SPOTIC_LYRICS_H
#include "api_contract.h"
#include "json_view.h"
#include "token_manager.h"
#include "audio/player.h"

#define LYRICS_LINE_CAP 2048u
#define LYRICS_DOCUMENT_CAP ((size_t)256 * 1024)
#define LYRICS_MODEL_BUDGET ((size_t)32 * 1024)
#define LYRICS_NO_LINE UINT32_MAX
typedef enum LyricsResult {
    LYRICS_OK, LYRICS_PENDING, LYRICS_FULL, LYRICS_INVALID, LYRICS_STALE,
    LYRICS_CANCELLED, LYRICS_AUTH, LYRICS_RATE_LIMIT, LYRICS_ERROR,
    LYRICS_TOO_LARGE, LYRICS_NO_MEMORY, LYRICS_CLOSED
} LyricsResult;
typedef enum LyricsSync { LYRICS_PLAIN, LYRICS_LINE_SYNCED, LYRICS_SYLLABLE_SYNCED } LyricsSync;
typedef enum LyricsAvailability { LYRICS_AVAILABLE, LYRICS_MISSING, LYRICS_EMPTY, LYRICS_CAPPED } LyricsAvailability;
typedef struct LyricsLine { uint64_t start_ms; JsonString text; } LyricsLine;
typedef struct LyricsDocument {
    JsonPage *owner;
    uint64_t generation;
    uint8_t track_id[16];
    LyricsSync sync;
    LyricsAvailability availability;
    JsonString raw_sync, cap_status, language, provider, provider_display_name, provider_id;
    uint32_t count, preview_count;
    bool vocal_removal, rtl;
    /* Full rows first, then preview rows. Capped documents expose preview rows
     * only (count=0); no invented word/syllable schema or full-rights grant. */
    LyricsLine lines[LYRICS_LINE_CAP];
} LyricsDocument;
typedef struct LyricsTrack {
    JsonString uri, artwork, client_language, market;
    bool vocal_removal;
} LyricsTrack;
typedef struct LyricsTicket { uint32_t slot; uint64_t serial; } LyricsTicket;
typedef struct LyricsRequest {
    LyricsTicket ticket;
    const ApiContract *contract;
    const wchar_t *url;
    char *body; size_t capacity;
    uint64_t generation;
} LyricsRequest;
typedef struct LyricsConfig {
    HttpClient *http; /* NULL: externally driven delivery with lyrics_complete. */
    TokenManager *tokens; /* Required for production HTTP; exact service grant. */
    const wchar_t *app_version, *accept_language; /* Borrowed session values. */
    const wchar_t *fixture_origin; /* Explicit http://127.0.0.1:PORT only. */
} LyricsConfig;
typedef struct LyricsService LyricsService;

/* ONE network owner; caller model storage plus the SAME JsonPagePool used by
 * collections/catalog. All storage/config/transport outlives final references.
 * With HTTP configured, poll drives bounded work; use the shared HTTP event and
 * its/token deadlines in the owner's existing wait. No module thread/timer. */
size_t lyrics_service_size(void);
size_t lyrics_service_alignment(void);
LyricsResult lyrics_service_init(void *storage, size_t bytes, JsonPagePool *pool,
    const LyricsConfig *config, LyricsService **output);
LyricsResult lyrics_reset(LyricsService *service, uint64_t generation);
LyricsResult lyrics_request(LyricsService *service, const LyricsTrack *track,
    uint64_t generation, LyricsRequest *output);
LyricsResult lyrics_cancel(LyricsService *service, LyricsTicket ticket);
/* External transport only: call once terminal, even on cancellation. 404 needs
 * no JSON body. Success publishes an owned reference, including MISSING/CAPPED.
 * All failures clear output. Buffers stay reserved until this acknowledgement. */
LyricsResult lyrics_complete(LyricsService *service, LyricsTicket ticket, LyricsResult result,
    unsigned status, size_t bytes, const LyricsDocument **output);
LyricsResult lyrics_poll(LyricsService *service, const LyricsDocument **output);
LyricsResult lyrics_shutdown(LyricsService *service);
bool lyrics_document_retain(const LyricsDocument *document);
bool lyrics_document_release(const LyricsDocument *document);
/* Pure, allocation-free. Last equal-time row wins. Plain/capped/missing or a
 * position before the first start has NO_LINE. Final line has no finite end. */
uint32_t lyrics_line_at(const LyricsDocument *document, uint64_t position_ms);
uint64_t lyrics_next_deadline(const LyricsDocument *document, const PlayerSnapshot *audible,
    bool visible, uint64_t now_ms);
typedef struct LyricsTimeline { const LyricsDocument *document; } LyricsTimeline;
typedef struct LyricsView { uint32_t line; uint64_t position_ms, deadline_ms; bool valid; } LyricsView;
/* Start at {0}; set retains its document; set(NULL) releases it. Refresh the
 * player on its owner before update. Deadlines request a fresh audio snapshot,
 * never permission to advance a line using wall time. Same-track seek/pause
 * epochs reuse lyrics; different track IDs suppress the old document. */
bool lyrics_timeline_set(LyricsTimeline *timeline, const LyricsDocument *document);
LyricsView lyrics_timeline_update(const LyricsTimeline *timeline, const PlayerSnapshot *audible,
    bool visible, uint64_t now_ms);
#endif
