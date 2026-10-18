#include "api/metadata_internal.h"
#include <string.h>

static void consume(MetadataReader *r, const pb_istream_t *stream, size_t before)
{
    size_t bytes = before - stream->bytes_left;
    r->span.data += bytes;
    r->span.bytes -= bytes;
}

bool metadata_next(MetadataReader *r, uint32_t *tag, pb_wire_type_t *wire)
{
    if (r->result != MEDIA_OK || r->span.bytes == 0) return false;
    if (++r->fields > 2048) { r->result = MEDIA_LIMIT; return false; }
    pb_istream_t stream = pb_istream_from_buffer(r->span.data, r->span.bytes);
    bool eof;
    if (!pb_decode_tag(&stream, wire, tag, &eof) || *tag == 0) {
        r->result = MEDIA_MALFORMED; return false;
    }
    consume(r, &stream, r->span.bytes);
    return true;
}

bool metadata_uint(MetadataReader *r, pb_wire_type_t wire, uint64_t *value)
{
    pb_istream_t stream = pb_istream_from_buffer(r->span.data, r->span.bytes);
    if (wire != PB_WT_VARINT || !pb_decode_varint(&stream, value)) {
        r->result = MEDIA_MALFORMED; return false;
    }
    consume(r, &stream, r->span.bytes);
    return true;
}

bool metadata_span(MetadataReader *r, pb_wire_type_t wire, MetadataSpan *span)
{
    uint32_t bytes;
    pb_istream_t stream = pb_istream_from_buffer(r->span.data, r->span.bytes);
    if (wire != PB_WT_STRING || !pb_decode_varint32(&stream, &bytes) || bytes > stream.bytes_left) {
        r->result = MEDIA_MALFORMED; return false;
    }
    consume(r, &stream, r->span.bytes);
    *span = (MetadataSpan){r->span.data, bytes};
    r->span.data += bytes; r->span.bytes -= bytes;
    return true;
}

bool metadata_skip(MetadataReader *r, pb_wire_type_t wire)
{
    pb_istream_t stream = pb_istream_from_buffer(r->span.data, r->span.bytes);
    if (!pb_skip_field(&stream, wire)) { r->result = MEDIA_MALFORMED; return false; }
    consume(r, &stream, r->span.bytes);
    return true;
}

bool metadata_once(MetadataReader *r, uint64_t *seen, uint32_t tag)
{
    if (tag >= 64 || (*seen & (UINT64_C(1) << tag)) != 0) {
        r->result = MEDIA_MALFORMED; return false;
    }
    *seen |= UINT64_C(1) << tag;
    return true;
}

bool metadata_equal(MetadataSpan span, const char *text)
{
    size_t bytes = strlen(text);
    return span.bytes == bytes && memcmp(span.data, text, bytes) == 0;
}
