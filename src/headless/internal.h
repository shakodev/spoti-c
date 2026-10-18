#ifndef SPOTIC_HEADLESS_INTERNAL_H
#define SPOTIC_HEADLESS_INTERNAL_H
#include "headless.h"
#include "audio/decrypt_stream.h"
#include "core/memory_budget.h"
#include "protocol/ap_session.h"
#include "protocol/ap_auth.h"
#include <string.h>

enum { HL_ARENAS = 12 };
enum { HL_IDLE, HL_REQUEST, HL_PREPARED, HL_DONE, HL_FAILED };
typedef struct HeadlessSource {
    struct Headless *host;
    PlayerRequest request;
    MediaId media_id;
    MediaSource media;
    RangeSource range;
    RangeId prefix, payload;
    DecryptStream stream;
    DecryptOperation operation;
    _Atomic unsigned phase;
    uint64_t offset, bytes;
    size_t capacity;
    bool used, ready, retiring, failed;
} HeadlessSource;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4324) /* Intentional cache-line alignment, fixed VM storage. */
#endif
struct Headless {
    HeadlessConfig config;
    wchar_t cache_path[240], credential_path[240];
    char fixture_url[MEDIA_URL_BYTES], country[3], catalogue[33];
    HANDLE ready, decode_ready, read_wake, network_thread, decode_thread;
    SRWLOCK lifecycle;
    _Atomic bool failed, closing, decode_finished;
    bool resources_released;
    Player *player;
    AudioSink sink;
    Arena arenas[HL_ARENAS];
    Metrics accounting;
    MetricsArena bindings[HL_ARENAS];
    Worker *disk;
    AudioCache *cache;
    HttpClient *http;
    MediaResolver *media;
    RangeFetcher *ranges;
    HeadlessSource sources[2];
    Crypto crypto;
    uint8_t scratch[DECRYPT_SCRATCH_MAX];
    _Atomic uint64_t frames, wakes, callback_ticks, callbacks, underruns;
    _Atomic uint64_t network_attempts, cache_hits;
    uint64_t device_frames;
    LARGE_INTEGER frequency;
    HeadlessMetrics final;
    CredentialRecord credentials;
    ApAuth auth;
    ApSession *ap;
    AudioKeyClient *keys;
    TokenManager *tokens;
    bool keys_connected;
    wchar_t origin[256];
    _Alignas(64) uint8_t core[MEMORY_BUDGET_CORE], protocol[MEMORY_BUDGET_PROTOCOL];
    _Alignas(16) uint8_t transport[MEMORY_BUDGET_HTTP], metadata[MEMORY_BUDGET_METADATA];
    _Alignas(16) uint8_t range_meta[65536], cache_meta[MEMORY_BUDGET_METADATA];
    _Alignas(16) uint8_t blocks[MEMORY_BUDGET_ENCRYPTED_BLOCKS];
    _Alignas(16) uint8_t codecs[2][DECODER_CODEC_BYTES], work[2][DECODER_WORK_BYTES];
    _Alignas(64) uint8_t pcm[MEMORY_BUDGET_PCM];
    _Alignas(16) uint8_t token_memory[TOKEN_SECRET_LIMIT];
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
bool hl_arenas(Headless *);
bool hl_network_open(Headless *);
bool hl_network_poll(Headless *);
void hl_network_wait(Headless *, DWORD);
bool hl_network_close(Headless *);
bool hl_sources(Headless *);
DWORD WINAPI hl_decode(void *);
AudioResult hl_read(void *, uint64_t, uint64_t, void *, size_t, size_t *);
bool hl_live_open(Headless *);
bool hl_live_poll(Headless *);
bool hl_live_close(Headless *);
bool hl_fixture_media(Headless *, HeadlessSource *);
bool hl_reply(Headless *, PlayerCommand);
#endif
