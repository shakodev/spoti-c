#include "range_internal.h"

RangeResult range_acquire(RangeFetcher *f, RangeSource source, uint64_t offset, size_t length, RangeLease *out)
{
    if (f == NULL || out == NULL || out->ticket != 0 || length == 0 || length > UINT64_MAX - offset) { return RANGE_INVALID; }
    if (f->busy) { return RANGE_BAD_STATE; }
    RangeSourceRecord *s = range_source(f, source); if (s == NULL || s->cancelled || f->shutdown) { return RANGE_STALE; }
    if (s->failure != RANGE_OK) { return s->failure; }
    if (s->size_known && offset >= s->total) { return RANGE_EOF; }
    if (length > MEMORY_BUDGET_ENCRYPTED_BLOCKS || (offset % RANGE_BLOCK_BYTES + length + RANGE_BLOCK_BYTES - 1) / RANGE_BLOCK_BYTES > RANGE_BLOCKS) { return RANGE_LIMIT; }
    size_t bytes = s->size_known && s->total - offset < length ? (size_t)(s->total - offset) : length;
    RangeLease lease = {.source = source}; uint8_t mask = 0; uint64_t at = offset;
    while (lease.bytes < bytes) {
        uint64_t start = at - at % RANGE_BLOCK_BYTES; RangeBlock *b = range_block(f, source, start);
        if (b == NULL || (b->state != RANGE_READY && b->state != RANGE_PINNED)) { return RANGE_PENDING; }
        if (b->failure != RANGE_OK) { return b->failure; }
        size_t inside = (size_t)(at - start); if (inside >= b->bytes) { return RANGE_EOF; }
        size_t take = b->bytes - inside; if (take > bytes - lease.bytes) { take = bytes - lease.bytes; }
        size_t index = (size_t)(b - f->blocks);
        lease.spans[lease.count++] = (RangeSpan){f->pool + index * RANGE_BLOCK_BYTES + inside, take, at};
        lease.bytes += take; at += take; mask |= (uint8_t)(1u << index);
    }
    if (f->ticket == UINT64_MAX) { return RANGE_EXHAUSTED; }
    for (size_t i = 0; i < RANGE_LEASES; ++i) {
        RangeTicket *ticket = &f->leases[i]; if (ticket->value != 0) { continue; }
        *ticket = (RangeTicket){++f->ticket, source, mask}; lease.ticket = ticket->value;
        for (size_t j = 0; j < RANGE_BLOCKS; ++j) {
            if ((mask & (1u << j)) != 0) {
                ++f->blocks[j].references; f->blocks[j].state = RANGE_PINNED;
                if (f->stamp != UINT64_MAX) { f->blocks[j].stamp = ++f->stamp; }
            }
        }
        *out = lease; return RANGE_OK;
    }
    return RANGE_FULL;
}
RangeResult range_release(RangeFetcher *f, RangeLease *lease)
{
    if (f == NULL || lease == NULL || lease->ticket == 0) { return RANGE_INVALID; }
    if (f->busy) { return RANGE_BAD_STATE; }
    for (size_t i = 0; i < RANGE_LEASES; ++i) {
        RangeTicket *t = &f->leases[i]; if (t->value != lease->ticket || !range_same(t->source, lease->source)) { continue; }
        uint8_t seen = 0; size_t bytes = 0;
        if (lease->count > RANGE_BLOCKS) { return RANGE_STALE; }
        for (size_t j = 0; j < lease->count; ++j) {
            const RangeSpan *span = &lease->spans[j]; bool found = false;
            for (size_t k = 0; k < RANGE_BLOCKS; ++k) {
                const RangeBlock *b = &f->blocks[k]; uintptr_t base = (uintptr_t)(f->pool + k * RANGE_BLOCK_BYTES), data = (uintptr_t)span->data;
                if ((t->blocks & (1u << k)) != 0 && data >= base && data - base < b->bytes &&
                    span->bytes <= b->bytes - (size_t)(data - base) && span->offset == b->offset + (data - base) &&
                    b->references != 0 && range_same(b->source, t->source)) {
                    if ((seen & (1u << k)) != 0) { return RANGE_STALE; }
                    seen |= (uint8_t)(1u << k); bytes += span->bytes; found = true; break;
                }
            }
            if (!found) { return RANGE_STALE; }
        }
        if (seen != t->blocks || bytes != lease->bytes) { return RANGE_STALE; }
        for (size_t k = 0; k < RANGE_BLOCKS; ++k) {
            if ((t->blocks & (1u << k)) != 0) { RangeBlock *b = &f->blocks[k]; --b->references; b->state = b->references == 0 ? RANGE_READY : RANGE_PINNED; }
        }
        memset(t, 0, sizeof(*t)); memset(lease, 0, sizeof(*lease)); range_reap_sources(f); f->needs_drive = true; return RANGE_OK;
    }
    return RANGE_STALE;
}
