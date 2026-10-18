#include "mercury_internal.h"
#include "pb_common.h"

bool mercury_text(const char *text, char output[MERCURY_URI + 1], bool empty)
{
    if (text == NULL) { return false; }
    size_t n = 0;
    while (n <= MERCURY_URI && text[n] != '\0') { ++n; }
    if (n > MERCURY_URI || (!empty && n == 0)) { return false; }
    memcpy(output, text, n + 1);
    return pb_validate_utf8(output);
}

ApResult mercury_create(Arena *arena, const MercuryConfig *config, MercuryClient **out)
{
    if (out == NULL) { return AP_INVALID; }
    *out = NULL;
    if (arena == NULL || config == NULL || config->send == NULL) { return AP_INVALID; }
    MercuryClient *c = arena_alloc(arena, sizeof(*c), _Alignof(MercuryClient));
    if (c == NULL) { return AP_NO_MEMORY; }
    memset(c, 0, sizeof(*c)); c->config = *config;
    if (c->config.timeout_ms == 0) { c->config.timeout_ms = 10000; }
    *out = c; return AP_OK;
}

static void put16(uint8_t *p, size_t value)
{
    p[0] = (uint8_t)(value >> 8); p[1] = (uint8_t)value;
}

ApResult mercury_start(MercuryClient *c, uint8_t command, const char *method,
    const char *uri, const char *content_type, const MercurySpan *parts, size_t count,
    uint64_t deadline, MercuryCallback callback, void *context, int subscription, MercuryId *id)
{
    if (!c->connected) { return AP_CLOSED; }
    if (id == NULL || callback == NULL || (parts == NULL && count != 0)) { return AP_INVALID; }
    if (count >= MERCURY_PARTS) { return AP_LIMIT; }
    MercuryPending *p = NULL;
    for (size_t i = 0; i < MERCURY_REQUESTS; ++i) {
        if (!c->pending[i].live) { p = &c->pending[i]; break; }
    }
    if (p == NULL) { return AP_FULL; }
    if (c->sequence == UINT64_MAX) { return AP_EXHAUSTED; }
    uint64_t now = core_clock_now(&c->config.clock);
    if (deadline == CORE_CLOCK_NEVER) { deadline = core_deadline_after(now, c->config.timeout_ms); }
    if (core_deadline_expired(now, deadline)) { return AP_TIMEOUT; }
    Header header = Header_init_zero;
    if (!mercury_text(uri, header.uri, false)) { return AP_INVALID; }
    header.has_uri = header.has_method = true;
    memcpy(header.method, method, strlen(method) + 1);
    if (content_type != NULL) {
        if (!mercury_text(content_type, header.content_type, true)) { return AP_INVALID; }
        header.has_content_type = true;
    }
    size_t header_bytes;
    if (proto_encode(Header_fields, &header, c->tx + 15, sizeof(c->tx) - 15, &header_bytes) != PROTO_OK) {
        return AP_INVALID;
    }
    size_t pos = 15 + header_bytes;
    for (size_t i = 0; i < count; ++i) {
        if (parts[i].data == NULL && parts[i].bytes != 0) { return AP_INVALID; }
        if (parts[i].bytes > UINT16_MAX || pos + 2 > sizeof(c->tx) ||
            parts[i].bytes > sizeof(c->tx) - pos - 2) { return AP_LIMIT; }
        put16(c->tx + pos, parts[i].bytes); pos += 2;
        if (parts[i].bytes != 0) { memcpy(c->tx + pos, parts[i].data, parts[i].bytes); }
        pos += parts[i].bytes;
    }
    uint64_t seq = ++c->sequence; /* Burn IDs even when transport rejects admission. */
    put16(c->tx, 8);
    for (size_t i = 0; i < 8; ++i) { c->tx[2 + i] = (uint8_t)(seq >> ((7 - i) * 8)); }
    c->tx[10] = 1; put16(c->tx + 11, count + 1); put16(c->tx + 13, header_bytes);
    ApResult result = c->config.send(c->config.context, command, c->tx, pos);
    if (result != AP_OK) { return result; }
    *p = (MercuryPending){0}; p->live = true; p->sequence = seq; p->deadline = deadline;
    p->command = command; p->subscription = subscription;
    p->callback = callback; p->context = context; memcpy(p->uri, header.uri, sizeof(p->uri));
    id->value = seq; return AP_OK;
}

ApResult mercury_request(MercuryClient *c, MercuryMethod method, const char *uri,
    const char *content_type, const MercurySpan *parts, size_t count, uint64_t deadline,
    MercuryCallback callback, void *context, MercuryId *id)
{
    if (c == NULL || (method != MERCURY_GET && method != MERCURY_SEND)) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    c->busy = true;
    ApResult result = mercury_start(c, MERCURY_REQ, method == MERCURY_GET ? "GET" : "SEND",
        uri, content_type, parts, count, deadline, callback, context, -1, id);
    c->busy = false; return result;
}

ApResult mercury_subscribe(MercuryClient *c, const char *uri, uint64_t deadline,
    MercuryCallback callback, void *context, MercuryId *id)
{
    if (c == NULL || id == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    size_t i;
    for (i = 0; i < MERCURY_SUBSCRIPTIONS && c->subs[i].live; ++i) {}
    if (i == MERCURY_SUBSCRIPTIONS) { return AP_FULL; }
    c->busy = true;
    ApResult result = mercury_start(c, MERCURY_SUB, "SUB", uri, NULL, NULL, 0,
        deadline, callback, context, (int)i, id);
    if (result == AP_OK) {
        MercurySub *s = &c->subs[i]; *s = (MercurySub){0};
        s->live = s->waiting = true; s->id = id->value; s->callback = callback; s->context = context;
        (void)mercury_text(uri, s->uri, false);
    }
    c->busy = false; return result;
}

ApResult mercury_unsubscribe(MercuryClient *c, MercuryId subscription, uint64_t deadline,
    MercuryCallback callback, void *context, MercuryId *id)
{
    if (c == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    MercurySub *s = NULL;
    for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) {
        if (c->subs[i].live && c->subs[i].id == subscription.value) { s = &c->subs[i]; break; }
    }
    if (s == NULL) { return AP_STALE; }
    if (!s->active || s->waiting) { return AP_BAD_STATE; }
    c->busy = true;
    ApResult result = mercury_start(c, MERCURY_UNSUB, "UNSUB", s->uri, NULL, NULL, 0,
        deadline, callback, context, -1, id);
    if (result == AP_OK) { memset(s, 0, sizeof(*s)); }
    c->busy = false; return result;
}

void mercury_finish(MercuryClient *c, MercuryPending *p, ApResult result, const MercuryResponse *response)
{
    MercuryCallback callback = p->callback; void *context = p->context;
    MercuryId id = {p->sequence};
    if (p->subscription >= 0) {
        MercurySub *s = &c->subs[p->subscription]; id.value = s->id; s->waiting = false;
        if (result != AP_OK) { memset(s, 0, sizeof(*s)); }
    }
    if (callback != NULL) { callback(context, id, result, response); }
    mercury_release(c, p);
}

ApResult mercury_cancel(MercuryClient *c, MercuryId id)
{
    if (c == NULL || id.value == 0) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    for (size_t i = 0; i < MERCURY_REQUESTS; ++i) {
        MercuryPending *p = &c->pending[i];
        if (p->live && p->sequence == id.value) {
            c->busy = true; mercury_finish(c, p, AP_CANCELLED, NULL); c->busy = false; return AP_OK;
        }
    }
    for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) {
        MercurySub *s = &c->subs[i];
        if (!s->live || s->id != id.value) { continue; }
        c->busy = true;
        for (size_t j = 0; j < MERCURY_REQUESTS; ++j) {
            MercuryPending *p = &c->pending[j];
            if (p->live && p->subscription == (int)i) { mercury_release(c, p); }
        }
        MercuryCallback callback = s->callback; void *context = s->context;
        memset(s, 0, sizeof(*s)); callback(context, id, AP_CANCELLED, NULL);
        c->busy = false; return AP_OK;
    }
    return AP_STALE;
}

ApResult mercury_disconnect(MercuryClient *c, bool reconnect)
{
    if (c == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    c->busy = true; c->connected = false;
    ApResult reason = reconnect ? AP_RECONNECT : AP_CLOSED;
    for (size_t i = 0; i < MERCURY_ACCUMULATORS; ++i) {
        MercuryPending *p = &c->pending[i];
        if (!p->live) { continue; }
        if (p->subscription >= 0 || p->command == MERCURY_EVENT) { mercury_release(c, p); }
        else { mercury_finish(c, p, reason, NULL); }
    }
    for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) {
        MercurySub *s = &c->subs[i];
        if (!s->live) { continue; }
        MercuryCallback callback = s->callback; void *context = s->context; MercuryId id = {s->id};
        if (reconnect && c->config.resubscribe) { s->active = s->waiting = false; s->count = 0; }
        else { memset(s, 0, sizeof(*s)); }
        callback(context, id, reason, NULL);
    }
    memset(c->recent, 0, sizeof(c->recent)); c->recent_count = c->recent_next = 0;
    c->busy = false; return AP_OK;
}

static ApResult tick(MercuryClient *c)
{
    uint64_t now = core_clock_now(&c->config.clock);
    for (size_t i = 0; i < MERCURY_ACCUMULATORS; ++i) {
        MercuryPending *p = &c->pending[i];
        if (p->live && core_deadline_expired(now, p->deadline)) { mercury_finish(c, p, AP_TIMEOUT, NULL); }
    }
    if (!c->connected) { return AP_OK; }
    for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) {
        MercurySub *s = &c->subs[i];
        if (!s->live || s->active || s->waiting) { continue; }
        MercuryId id;
        ApResult result = mercury_start(c, MERCURY_SUB, "SUB", s->uri, NULL, NULL, 0,
            CORE_CLOCK_NEVER, s->callback, s->context, (int)i, &id);
        if (result == AP_FULL) { return AP_OK; }
        if (result != AP_OK) { return result; }
        s->waiting = true;
    }
    return AP_OK;
}

ApResult mercury_connect(MercuryClient *c, uint32_t generation)
{
    if (c == NULL || generation == 0) { return AP_INVALID; }
    if (c->busy || c->connected) { return AP_BAD_STATE; }
    c->generation = generation; c->connected = c->busy = true;
    ApResult result = tick(c); c->busy = false; return result;
}

ApResult mercury_tick(MercuryClient *c)
{
    if (c == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    c->busy = true; ApResult result = tick(c); c->busy = false; return result;
}

uint32_t mercury_next_wait_ms(const MercuryClient *c)
{
    if (c == NULL) { return UINT32_MAX; }
    uint64_t deadline = CORE_CLOCK_NEVER;
    for (size_t i = 0; i < MERCURY_ACCUMULATORS; ++i) {
        if (c->pending[i].live && c->pending[i].deadline < deadline) { deadline = c->pending[i].deadline; }
    }
    return core_deadline_wait(core_clock_now(&c->config.clock), deadline);
}

MercuryStats mercury_stats(const MercuryClient *c)
{
    MercuryStats stats = {0};
    if (c == NULL) { return stats; }
    stats.pool_used = c->used; stats.pool_high_water = c->high_water;
    for (size_t i = 0; i < MERCURY_ACCUMULATORS; ++i) {
        if (c->pending[i].live) { if (i < MERCURY_REQUESTS) { ++stats.pending; } else { ++stats.events; } }
    }
    for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) { stats.subscriptions += c->subs[i].live ? 1u : 0u; }
    return stats;
}

ApResult mercury_on_packet(MercuryClient *c, uint32_t generation, const ApPacket *packet)
{
    if (c == NULL || packet == NULL) { return AP_INVALID; }
    if (c->busy) { return AP_BAD_STATE; }
    if (!c->connected) { return AP_CLOSED; }
    if (generation != c->generation) { return AP_STALE; }
    ApPacketView view;
    ApResult result = ap_packet_peek(packet, &view);
    if (result != AP_OK) { return result; }
    c->busy = true; result = mercury_dispatch(c, &view); c->busy = false; return result;
}
