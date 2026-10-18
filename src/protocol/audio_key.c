#include "audio_key_internal.h"
#include <string.h>

ApResult audio_key_create(Arena *arena, const AudioKeyConfig *config, AudioKeyClient **out)
{
    if (out == NULL) { return AP_INVALID; }
    *out = NULL;
    if (arena == NULL || config == NULL || config->send == NULL) { return AP_INVALID; }
    AudioKeyClient *c = arena_alloc(arena, sizeof(*c), _Alignof(AudioKeyClient));
    if (c == NULL) { return AP_NO_MEMORY; }
    memset(c, 0, sizeof(*c)); c->config = *config;
    if (c->config.timeout_ms == 0) { c->config.timeout_ms = 1500; }
    *out = c; return AP_OK;
}

ApResult audio_key_connect(AudioKeyClient *c, uint32_t generation)
{
    if (c == NULL || generation == 0) { return AP_INVALID; }
    if (c->busy || c->connected) { return AP_BAD_STATE; }
    if (generation < c->generation) { return AP_STALE; }
    c->generation = generation; c->connected = true; return AP_OK;
}

static void finish(AudioKeyClient *c, AudioKeyPending *p, ApResult result, const uint8_t *key)
{
    AudioKeyCallback callback = p->callback; void *context = p->context;
    AudioKeyId id = {p->sequence};
    memset(p, 0, sizeof(*p)); --c->stats.pending;
    callback(context, id, result, key);
}

ApResult audio_key_disconnect(AudioKeyClient *c, bool reconnect)
{
    if (c == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    c->busy = true; c->connected = false;
    for (size_t i = 0; i < AUDIO_KEY_REQUESTS; ++i) {
        AudioKeyPending *p = &c->pending[i];
        if (p->callback != NULL) { finish(c, p, reconnect ? AP_RECONNECT : AP_CLOSED, NULL); }
    }
    c->busy = false; return AP_OK;
}

ApResult audio_key_request(AudioKeyClient *c, const uint8_t gid[16], const uint8_t file_id[20],
    uint64_t deadline, AudioKeyCallback callback, void *context, AudioKeyId *id)
{
    if (c == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    if (!c->connected) { return AP_CLOSED; }
    if (gid == NULL || file_id == NULL || callback == NULL || id == NULL) { return AP_INVALID; }
    AudioKeyPending *p = NULL;
    for (size_t i = 0; i < AUDIO_KEY_REQUESTS; ++i) {
        if (c->pending[i].callback == NULL) { p = &c->pending[i]; break; }
    }
    if (p == NULL) { return AP_FULL; }
    if (c->sequence == UINT32_MAX) { return AP_EXHAUSTED; }
    uint64_t now = core_clock_now(&c->config.clock);
    uint64_t limit = core_deadline_after(now, c->config.timeout_ms);
    if (deadline > limit) { deadline = limit; }
    if (core_deadline_expired(now, deadline)) { return AP_TIMEOUT; }
    uint8_t body[42];
    memcpy(body, file_id, 20); memcpy(body + 20, gid, 16);
    uint32_t seq = ++c->sequence; /* Burn even rejected sends: never reuse a wire ID. */
    body[36] = (uint8_t)(seq >> 24); body[37] = (uint8_t)(seq >> 16);
    body[38] = (uint8_t)(seq >> 8); body[39] = (uint8_t)seq;
    body[40] = 0; body[41] = 0;
    c->busy = true;
    ApResult result = c->config.send(c->config.context, AUDIO_KEY_REQUEST, body, sizeof(body));
    if (result == AP_OK) {
        *p = (AudioKeyPending){seq, deadline, callback, context};
        ++c->stats.pending;
        if (c->stats.pending > c->stats.pending_high_water) { c->stats.pending_high_water = c->stats.pending; }
        id->value = seq;
    }
    c->busy = false; return result;
}

ApResult audio_key_cancel(AudioKeyClient *c, AudioKeyId id)
{
    if (c == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    if (id.value == 0) { return AP_INVALID; }
    for (size_t i = 0; i < AUDIO_KEY_REQUESTS; ++i) {
        AudioKeyPending *p = &c->pending[i];
        if (p->callback != NULL && p->sequence == id.value) {
            c->busy = true; finish(c, p, AP_CANCELLED, NULL); c->busy = false; return AP_OK;
        }
    }
    return AP_STALE;
}

static ApResult dispatch(AudioKeyClient *c, const ApPacketView *view)
{
    if (view->command != AUDIO_KEY_RESPONSE && view->command != AUDIO_KEY_ERROR) { return AP_UNSUPPORTED; }
    if (view->bytes < 4) { return AP_MALFORMED; }
    const uint8_t *b = view->body;
    uint32_t sequence = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    AudioKeyPending *p = NULL;
    for (size_t i = 0; i < AUDIO_KEY_REQUESTS; ++i) {
        if (c->pending[i].callback != NULL && c->pending[i].sequence == sequence) { p = &c->pending[i]; break; }
    }
    if (p == NULL) { return AP_STALE; }
    ApResult result;
    if (core_deadline_expired(core_clock_now(&c->config.clock), p->deadline)) { result = AP_TIMEOUT; }
    else if (view->bytes != (view->command == AUDIO_KEY_RESPONSE ? 20u : 6u)) { result = AP_MALFORMED; }
    else { result = view->command == AUDIO_KEY_RESPONSE ? AP_OK : AP_IO_ERROR; }
    finish(c, p, result, result == AP_OK ? b + 4 : NULL); return result;
}

ApResult audio_key_on_packet(AudioKeyClient *c, uint32_t generation, const ApPacket *packet)
{
    if (c == NULL || packet == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    if (!c->connected) { return AP_CLOSED; }
    if (generation != c->generation) { return AP_STALE; }
    ApPacketView view;
    ApResult result = ap_packet_peek(packet, &view);
    if (result != AP_OK) { return result; }
    c->busy = true; result = dispatch(c, &view); c->busy = false; return result;
}

ApResult audio_key_tick(AudioKeyClient *c)
{
    if (c == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    uint64_t now = core_clock_now(&c->config.clock);
    c->busy = true;
    for (size_t i = 0; i < AUDIO_KEY_REQUESTS; ++i) {
        AudioKeyPending *p = &c->pending[i];
        if (p->callback != NULL && core_deadline_expired(now, p->deadline)) { finish(c, p, AP_TIMEOUT, NULL); }
    }
    c->busy = false; return AP_OK;
}

uint32_t audio_key_next_wait_ms(const AudioKeyClient *c)
{
    if (c == NULL) { return UINT32_MAX; }
    uint64_t deadline = CORE_CLOCK_NEVER;
    for (size_t i = 0; i < AUDIO_KEY_REQUESTS; ++i) {
        if (c->pending[i].callback != NULL && c->pending[i].deadline < deadline) { deadline = c->pending[i].deadline; }
    }
    return core_deadline_wait(core_clock_now(&c->config.clock), deadline);
}

AudioKeyStats audio_key_stats(const AudioKeyClient *c)
{
    return c == NULL ? (AudioKeyStats){0} : c->stats;
}
