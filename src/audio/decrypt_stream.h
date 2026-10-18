#ifndef SPOTIC_DECRYPT_STREAM_H
#define SPOTIC_DECRYPT_STREAM_H
#include "range_fetcher.h"
#include "spotify_header.h"
#include "protocol/crypto.h"
#include <stdatomic.h>

enum { DECRYPT_SCRATCH_MAX = 65536 };
typedef enum DecryptResult { DECRYPT_OK, DECRYPT_PENDING, DECRYPT_INVALID,
    DECRYPT_LIMIT, DECRYPT_FULL, DECRYPT_STALE, DECRYPT_EOF, DECRYPT_UNSUPPORTED,
    DECRYPT_PREFIX_TRUNCATED, DECRYPT_PREFIX_CORRUPT, DECRYPT_CRYPTO,
    DECRYPT_RANGE_ERROR, DECRYPT_BAD_STATE } DecryptResult;
typedef struct DecryptOperation DecryptOperation;
typedef struct DecryptStream {
    MediaResolver *media; RangeFetcher *ranges;
    MediaKeyHandle key; RangeSource source; int32_t format;
    DecryptOperation *active;
    bool retired;
} DecryptStream;
/* Private fields exposed for fixed caller storage; never edit/copy while live. */
struct DecryptOperation {
    DecryptOperation *self;
    DecryptStream *owner;
    MediaKeyLease key;
    RangeLease prefix, payload;
    uint64_t logical, encrypted;
    size_t bytes;
    int32_t format;
    _Atomic unsigned phase;
};
_Static_assert(sizeof(DecryptOperation) < 1024, "bounded read operation metadata");
/* Owner thread. Initialize stream/operation storage at {0}; keep dependencies
 * alive through retirement and finish. Both handles MUST identify the same file.
 * This module admits only one operation per stream, no heap or owned handles. */
DecryptResult decrypt_stream_init(DecryptStream *, MediaResolver *, RangeFetcher *,
    const MediaSource *, RangeSource);
/* Owner thread; acquire BEFORE consuming range_status. Known file size needed.
 * PENDING leaves no leases: request [0,172) and mapped payload on the range
 * owner, poll, retry. No hidden HTTP requests/reservations. bytes <= 65536.
 * Each operation acquires its own generation-safe key ticket, including EOF. */
DecryptResult decrypt_stream_prepare(DecryptStream *, uint64_t logical, size_t bytes, DecryptOperation *);
/* Decoder thread, once per prepared operation. No owner API calls. scratch and
 * output must be disjoint from each other, all input/context storage, and
 * output metadata. scratch capacity 172..65536, >= clamped bytes. On error,
 * output is unchanged and *read=0. A claimed operation wipes valid scratch on
 * every exit. Crypto is decoder-owned
 * and initialized, used to audit provider handles/faults. Header output optional.
 * A handoff event/queue must publish the operation before dispatching here. */
DecryptResult decrypt_read_at(DecryptOperation *, Crypto *, void *scratch, size_t capacity,
    void *output, size_t output_capacity, size_t *read, SpotifyHeader *header);
/* Owner thread, after worker completion, or cancels an undispatched operation.
 * Returns PENDING while executing. Exact tickets release once; zeroes operation.
 * Worker completion uses release/acquire publication before leases can retire.
 * Do not dispatch concurrently with cancellation of an unstarted operation.
 * BAD_STATE during dependency release retains tickets for an owner retry. */
DecryptResult decrypt_stream_finish(DecryptStream *, DecryptOperation *);
DecryptResult decrypt_stream_retire(DecryptStream *); /* PENDING until finished. */
#endif
