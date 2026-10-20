#include "internal.h"
static void resolved(void *ctx, MediaId id, MediaResult r) { (void)ctx; (void)id; (void)r; }

bool hl_reply(Headless *h, PlayerCommand c)
{
    PlayerResult r = player_submit(h->player, &c);
    return r == PLAYER_OK || r == PLAYER_STALE;
}
AudioResult hl_read(void *context, uint64_t ticket, uint64_t offset, void *out, size_t cap, size_t *got)
{
    HeadlessSource *s = context; Headless *h = s->host; *got = 0;
    if (s->request.ticket != ticket) { return AUDIO_STALE; }
    unsigned phase = atomic_load_explicit(&s->phase, memory_order_acquire);
    if (phase == HL_FAILED) { return AUDIO_CORRUPT; }
    if (phase == HL_PREPARED) {
        AudioResult result = AUDIO_NEED_MORE;
        if (s->offset == offset && s->capacity <= cap) {
            DecryptResult r = decrypt_read_at(&s->operation, &h->crypto, h->scratch, sizeof(h->scratch), out, cap, got, NULL);
            if (r != DECRYPT_OK) { fprintf(stderr,"headless decrypt result=%u offset=%llu cap=%zu\n",(unsigned)r,(unsigned long long)offset,cap); }
            result = r == DECRYPT_OK ? (offset + *got == s->bytes ? AUDIO_EOF : AUDIO_OK) : AUDIO_CORRUPT;
        }
        /* A superseding seek may abandon an undispatched operation. Only the
         * network owner finishes/releases it, after this acknowledgement. */
        atomic_store_explicit(&s->phase, HL_DONE, memory_order_release); SetEvent(h->read_wake);
        return result;
    }
    if (phase != HL_IDLE) { return AUDIO_NEED_MORE; }
    s->offset = offset; s->capacity = cap > DECRYPT_SCRATCH_MAX ? DECRYPT_SCRATCH_MAX : cap;
    atomic_store_explicit(&s->phase, HL_REQUEST, memory_order_release); SetEvent(h->read_wake);
    return AUDIO_NEED_MORE;
}
static RangeResult status(Headless *h, RangeId id)
{
    return id.value ? range_status(h->ranges, id, false) : RANGE_OK;
}
static void consume(Headless *h, RangeId *id)
{
    if (id->value) { (void)range_status(h->ranges, *id, true); id->value = 0; }
}
static bool retire(Headless *h, HeadlessSource *s)
{
    if (s->stream.active && decrypt_stream_finish(&s->stream, &s->operation) != DECRYPT_OK) { return false; }
    if (s->stream.media) { (void)decrypt_stream_retire(&s->stream); }
    if (s->range.generation) { (void)range_cancel_generation(h->ranges, s->range); }
    /* Cancellation completes reservations; release them before slot reuse. */
    consume(h, &s->prefix); consume(h, &s->payload);
    if (s->media_id.generation) {
        MediaResult r = media_source_retire(h->media, s->media_id);
        if (r != MEDIA_OK && r != MEDIA_STALE) { return false; }
    }
    uint64_t ticket = s->request.ticket;
    if (!hl_reply(h, (PlayerCommand){.type = PLAYER_SOURCE_RETIRED, .ticket = ticket})) { return false; }
    SecureZeroMemory(s, sizeof(*s)); atomic_init(&s->phase, HL_IDLE); return true;
}
static bool prepare_read(Headless *h, HeadlessSource *s)
{
    if (s->operation.self) {
        if (decrypt_stream_finish(&s->stream, &s->operation) != DECRYPT_OK) { return false; }
    }
    if (s->prefix.value || s->payload.value) {
        RangeResult a = status(h, s->prefix), b = status(h, s->payload);
        if ((a != RANGE_OK && a != RANGE_PENDING) || (b != RANGE_OK && b != RANGE_PENDING)) {
            fprintf(stderr,"headless read status prefix=%u payload=%u\n",(unsigned)a,(unsigned)b); return false;
        }
        if (a == RANGE_PENDING || b == RANGE_PENDING) { return true; }
        DecryptResult r = decrypt_stream_prepare(&s->stream, s->offset, s->capacity, &s->operation);
        if (r != DECRYPT_OK) { fprintf(stderr,"headless read prepare=%u\n",(unsigned)r); return false; }
        consume(h, &s->prefix); consume(h, &s->payload);
        atomic_store_explicit(&s->phase, HL_PREPARED, memory_order_release);
        return hl_reply(h, (PlayerCommand){.type = PLAYER_DATA_READY, .ticket = s->request.ticket});
    }
    RangeResult prefix=range_request(h->ranges, s->range, 0, SPOTIFY_OGG_PROBE, RANGE_CURRENT, &s->prefix);
    if (prefix!=RANGE_OK) { fprintf(stderr,"headless prefix admission=%u\n",(unsigned)prefix); return false; }
    uint64_t encrypted = s->offset + SPOTIFY_OGG_PREFIX;
    if (encrypted < s->bytes + SPOTIFY_OGG_PREFIX && s->capacity) {
        size_t n = s->capacity;
        if (n > s->bytes - s->offset) { n = (size_t)(s->bytes - s->offset); }
        RangeResult payload=range_request(h->ranges, s->range, encrypted, n, s->request.prefetch ? RANGE_PREFETCH : RANGE_CURRENT, &s->payload);
        if (payload!=RANGE_OK) { fprintf(stderr,"headless payload admission=%u\n",(unsigned)payload); return false; }
    }
    return true;
}
bool hl_sources(Headless *h)
{
    PlayerRequest r;
    while (player_take_request(h->player, &r) == PLAYER_OK) {
        HeadlessSource *s = NULL;
        for (unsigned i = 0; i < 2; ++i) {
            if ((r.type == PLAYER_RETIRE && h->sources[i].used && h->sources[i].request.ticket == r.ticket) ||
                (r.type == PLAYER_RESOLVE && !h->sources[i].used)) { s = &h->sources[i]; break; }
        }
        if (!s) { return false; }
        if (r.type == PLAYER_RETIRE) { s->retiring = true; continue; }
        s->host = h; s->used = true; s->request = r;
        if (h->config.fixture_url) {
            if (!hl_fixture_media(h, s)) { s->failed = true; }
        } else {
            char uri[MEDIA_URI_BYTES];
            if (media_gid_to_uri(r.track_id, uri) != MEDIA_OK ||
                media_source_resolve(h->media, uri, resolved, NULL, &s->media_id) != MEDIA_OK) { s->failed = true; }
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        HeadlessSource *s = &h->sources[i]; if (!s->used) { continue; }
        if (s->retiring) { if (!retire(h, s)) { return false; } continue; }
        if (!s->failed && !s->ready) {
            MediaResult mr = media_source_get(h->media, s->media_id, &s->media);
            if (mr == MEDIA_PENDING || mr == MEDIA_EXPIRED) { continue; }
            if (mr != MEDIA_OK) {
                fprintf(stderr,"headless media result=%u token=%u\n",(unsigned)mr,(unsigned)token_result(h->tokens)); s->failed = true;
            }
            else {
                if (!s->range.generation) {
                    hl_publish_display(h,s->request.track_id,&s->media.track.display);
                    RangeResult open = range_source_open(h->ranges, &s->media, &s->range);
                    DecryptResult init = decrypt_stream_init(&s->stream, h->media, h->ranges, &s->media, s->range);
                    RangeResult request = range_request(h->ranges, s->range, 0, SPOTIFY_OGG_PROBE, RANGE_CURRENT, &s->prefix);
                    if (open != RANGE_OK || init != DECRYPT_OK || request != RANGE_OK) {
                        fprintf(stderr,"headless source open=%u init=%u request=%u\n",(unsigned)open,(unsigned)init,(unsigned)request); s->failed = true;
                    }
                }
                uint64_t total;
                RangeResult first = status(h, s->prefix);
                if (first == RANGE_PENDING) { continue; }
                if (first != RANGE_OK) { s->failed = true; }
                RangeResult rr = range_source_size(h->ranges, s->range, &total);
                if (rr == RANGE_PENDING && !s->failed) { continue; }
                if (rr != RANGE_OK || total <= SPOTIFY_OGG_PROBE) { s->failed = true; }
                if (!s->failed) {
                    consume(h, &s->prefix);
                    s->bytes = total - SPOTIFY_OGG_PREFIX; s->ready = true;
                    if (!hl_reply(h, (PlayerCommand){.type = PLAYER_SOURCE_READY, .ticket = s->request.ticket,
                        .source = {.bytes=s->bytes,.read_at=hl_read,.context=s,.duration_ms=s->media.track.duration_ms,
                            .trailing_zero_limit=h->config.fixture_url ? 0 : 15}})) { return false; }
                }
            }
        }
        unsigned phase = atomic_load_explicit(&s->phase, memory_order_acquire);
        if (phase == HL_DONE) {
            if (s->operation.self && decrypt_stream_finish(&s->stream, &s->operation) != DECRYPT_OK) { return false; }
            atomic_store_explicit(&s->phase, HL_IDLE, memory_order_release);
            if (!hl_reply(h, (PlayerCommand){.type = PLAYER_DATA_READY, .ticket = s->request.ticket})) { return false; }
        } else if (phase == HL_REQUEST && !s->failed && !prepare_read(h, s)) { s->failed = true; }
        if (s->failed && phase != HL_FAILED) {
            fprintf(stderr,"headless source failed ready=%u phase=%u\n",s->ready,phase);
            atomic_store_explicit(&s->phase, HL_FAILED, memory_order_release);
            PlayerCommand c = {.type = s->ready ? PLAYER_DATA_READY : PLAYER_SOURCE_FAILED,
                .ticket = s->request.ticket, .error = AUDIO_CORRUPT};
            if (!hl_reply(h, c)) { return false; }
        }
    }
    return true;
}
