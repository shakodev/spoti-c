#ifndef SPOTIC_MEDIA_TEST_H
#define SPOTIC_MEDIA_TEST_H
#include "protocol/media_source.h"
#include "support/test_check.h"
#include "pb_encode.h"
#include <string.h>
typedef struct MediaWire { uint8_t data[16384]; size_t bytes; } MediaWire;
static inline void media_number(MediaWire *out, uint32_t tag, uint64_t value)
{
    pb_ostream_t s = pb_ostream_from_buffer(out->data + out->bytes, sizeof(out->data) - out->bytes);
    CHECK(pb_encode_tag(&s, PB_WT_VARINT, tag) && pb_encode_varint(&s, value)); out->bytes += s.bytes_written;
}
static inline void media_blob(MediaWire *out, uint32_t tag, const void *data, size_t bytes)
{
    pb_ostream_t s = pb_ostream_from_buffer(out->data + out->bytes, sizeof(out->data) - out->bytes);
    CHECK(pb_encode_tag(&s, PB_WT_STRING, tag) && pb_encode_string(&s, data, bytes)); out->bytes += s.bytes_written;
}
static inline void media_text(MediaWire *out, uint32_t tag, const char *text)
{
    media_blob(out, tag, text, strlen(text));
}
static inline void media_message(MediaWire *out, uint32_t tag, const MediaWire *message)
{
    media_blob(out, tag, message->data, message->bytes);
}
void metadata_tests(void);
void storage_tests(void);
#endif
