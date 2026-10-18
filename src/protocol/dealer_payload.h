#ifndef SPOTIC_DEALER_PAYLOAD_H
#define SPOTIC_DEALER_PAYLOAD_H
#include "core/arena.h"
#include "yyjson.h"
#include "miniz_tinfl.h"

#define DEALER_POOL_BYTES ((size_t)128 * 1024)
#define DEALER_PAYLOAD_CAP (DEALER_POOL_BYTES - YYJSON_PADDING_SIZE)
typedef enum DealerResult { DEALER_OK, DEALER_PENDING, DEALER_FULL, DEALER_INVALID,
    DEALER_LIMIT, DEALER_MALFORMED, DEALER_IO, DEALER_AUTH, DEALER_CLOSED,
    DEALER_UNSUPPORTED, DEALER_STALE } DealerResult;

typedef struct DealerEnvelope {
    bool request, gzip;
    char uri[257], key[257], connection_id[257];
    unsigned char *payload;
    size_t bytes;
} DealerEnvelope;
/* Strict canonical base64; input and output may be the same buffer. */
DealerResult dealer_base64(const char *, size_t, void *, size_t, size_t *);
uint32_t dealer_crc32(const void *, size_t);
/* One complete RFC1952 member, including FHCRC, CRC32 and ISIZE. No heap.
 * Rejects reserved flags, concatenation/trailing bytes and expansion >cap. */
DealerResult dealer_gzip(tinfl_decompressor *, const void *, size_t, void *, size_t, size_t *);
/* Bounded strict JSON, caller-supplied allocator only. Input is modified. */
yyjson_doc *dealer_json(char *, size_t, void *, size_t);
bool dealer_string(yyjson_val *, char *, size_t);
bool dealer_u32(yyjson_val *, uint32_t, uint32_t *);
/* wire includes YYJSON_PADDING_SIZE bytes beyond 128 KiB. scratch is 128 KiB,
 * reused for DOM then inflate then command DOM; no retained DOM/payload copies. */
DealerResult dealer_envelope(unsigned char *wire, size_t bytes, void *scratch,
    tinfl_decompressor *, DealerEnvelope *);
#endif
