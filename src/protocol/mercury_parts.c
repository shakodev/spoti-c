#include "mercury_internal.h"

static size_t get16(const uint8_t *p) { return ((size_t)p[0] << 8) | p[1]; }

/* A packed pool makes capacity depend on live bytes, not fragmentation. Only
 * owner-thread ingress/release moves storage; callbacks cannot reenter it. */
void mercury_release(MercuryClient *c, MercuryPending *p)
{
    if (p->live && p->command == MERCURY_EVENT) {
        c->recent[c->recent_next] = p->sequence;
        c->recent_next = (c->recent_next + 1) % MERCURY_REQUESTS;
        if (c->recent_count < MERCURY_REQUESTS) { ++c->recent_count; }
    }
    if (p->bytes != 0) {
        size_t end = p->offset + p->bytes;
        memmove(c->pool + p->offset, c->pool + end, c->used - end);
        for (size_t i = 0; i < MERCURY_ACCUMULATORS; ++i) {
            MercuryPending *other = &c->pending[i];
            if (other != p && other->live && other->bytes != 0 && other->offset >= end) {
                other->offset -= p->bytes;
            }
        }
        c->used -= p->bytes; memset(c->pool + c->used, 0, p->bytes);
    }
    memset(p, 0, sizeof(*p));
}

static void append(MercuryClient *c, MercuryPending *p, const uint8_t *data, size_t bytes)
{
    if (bytes == 0) { return; }
    if (p->bytes == 0) { p->offset = c->used; }
    size_t end = p->offset + p->bytes;
    memmove(c->pool + end + bytes, c->pool + end, c->used - end);
    for (size_t i = 0; i < MERCURY_ACCUMULATORS; ++i) {
        MercuryPending *other = &c->pending[i];
        if (other != p && other->live && other->bytes != 0 && other->offset >= end) { other->offset += bytes; }
    }
    memcpy(c->pool + end, data, bytes); p->bytes += bytes; c->used += bytes;
    if (c->used > c->high_water) { c->high_water = c->used; }
}

static MercuryPending *find(MercuryClient *c, uint8_t command, uint64_t sequence)
{
    size_t first = command == MERCURY_EVENT ? MERCURY_REQUESTS : 0;
    size_t last = command == MERCURY_EVENT ? MERCURY_ACCUMULATORS : MERCURY_REQUESTS;
    for (size_t i = first; i < last; ++i) {
        MercuryPending *p = &c->pending[i];
        if (p->live && p->sequence == sequence && p->command == command) { return p; }
    }
    return NULL;
}

ApResult mercury_dispatch(MercuryClient *c, const ApPacketView *view)
{
    if (view->command < MERCURY_REQ || view->command > MERCURY_EVENT) { return AP_UNSUPPORTED; }
    if (view->bytes < 13 || get16(view->body) != 8) { return AP_MALFORMED; }
    const uint8_t *data = view->body;
    uint64_t sequence = 0;
    for (size_t i = 0; i < 8; ++i) { sequence = (sequence << 8) | data[2 + i]; }
    MercuryPending *p = find(c, view->command, sequence);
    size_t count = get16(data + 11), pos = 13, total = 0;
    uint8_t flags = data[10];
    ApResult error = AP_OK;
    if (flags > 2 || (flags != 1 && count == 0)) { error = AP_MALFORMED; }
    else if (count > MERCURY_PARTS) { error = AP_LIMIT; }
    /* Validate the entire packet before mutating state, including empty parts,
     * truncated lengths and surplus bytes. No unchecked pointer arithmetic. */
    if (error == AP_OK) {
        for (size_t i = 0; i < count; ++i) {
            if (view->bytes - pos < 2) { error = AP_MALFORMED; break; }
            size_t bytes = get16(data + pos); pos += 2;
            if (bytes > view->bytes - pos) { error = AP_MALFORMED; break; }
            total += bytes; pos += bytes;
        }
        if (pos != view->bytes) { error = AP_MALFORMED; }
    }
    if (error != AP_OK) {
        if (p != NULL) { mercury_finish(c, p, error, NULL); }
        return error;
    }
    if (p == NULL && view->command != MERCURY_EVENT) { return AP_STALE; }
    if (p == NULL) {
        for (size_t i = 0; i < c->recent_count; ++i) {
            if (c->recent[i] == sequence) { return AP_STALE; }
        }
        for (size_t i = MERCURY_REQUESTS; i < MERCURY_ACCUMULATORS; ++i) {
            if (!c->pending[i].live) { p = &c->pending[i]; break; }
        }
        if (p == NULL) { return AP_FULL; }
        *p = (MercuryPending){0}; p->live = true; p->command = MERCURY_EVENT;
        p->subscription = -1; p->sequence = sequence;
        p->deadline = core_deadline_after(core_clock_now(&c->config.clock), c->config.timeout_ms);
        for (size_t i = 0; i < MERCURY_SUBSCRIPTIONS; ++i) {
            if (c->subs[i].live && c->subs[i].active) { p->listeners[i] = c->subs[i].id; }
        }
    }
    if (core_deadline_expired(core_clock_now(&c->config.clock), p->deadline)) { error = AP_TIMEOUT; }
    else if ((p->partial && count == 0) || (p->count == 0 && count == 0)) { error = AP_MALFORMED; }
    else if (p->count + count - (p->partial ? 1u : 0u) > MERCURY_PARTS ||
        p->fragments == MERCURY_FRAGMENTS || total > MERCURY_RESPONSE_MAX - p->bytes) { error = AP_LIMIT; }
    else if (total > MERCURY_POOL - c->used) { error = AP_FULL; }
    if (error != AP_OK) { mercury_finish(c, p, error, NULL); return error; }
    pos = 13; ++p->fragments;
    for (size_t i = 0; i < count; ++i) {
        size_t bytes = get16(data + pos); pos += 2;
        if (!p->partial) { p->lengths[p->count++] = 0; }
        p->lengths[p->count - 1] += bytes;
        append(c, p, data + pos, bytes); pos += bytes;
        p->partial = i + 1 == count && flags == 2;
    }
    if (flags != 1) { return AP_PENDING; }
    return mercury_complete(c, p);
}
