#ifndef SPOTIC_PROTO_TEST_H
#define SPOTIC_PROTO_TEST_H
#include "protocol/proto_codec.h"
#include "support/test_check.h"
#include "pb_encode.h"
#include "pb_decode.h"
#include <string.h>

typedef struct ProtoSlice { const uint8_t *data; size_t size; } ProtoSlice;
static inline bool emit_slice(pb_ostream_t *stream, const pb_field_t *field, void * const *arg)
{
    const ProtoSlice *slice = *arg;
    return pb_encode_tag_for_field(stream, field) && pb_encode_string(stream, slice->data, slice->size);
}
static inline void golden(const pb_msgdesc_t *desc, const void *message,
                          const uint8_t *expected, size_t size)
{
    uint8_t buffer[4096];
    size_t written = 0;
    CHECK(proto_encode(desc, message, buffer, sizeof(buffer), &written) == PROTO_OK);
    CHECK(written == size && memcmp(buffer, expected, size) == 0);
}
void test_proto_messages(void);
#endif
