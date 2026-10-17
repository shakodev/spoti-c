#include "protocol/proto_codec.h"
#include "pb_common.h"
#include "pb_decode.h"
#include "pb_encode.h"
#include <string.h>

enum { MAX_FIELDS = 128 };

static ProtoResult validate_message(pb_istream_t *stream, const pb_msgdesc_t *desc,
                                    unsigned depth);

static pb_wire_type_t scalar_wire(pb_type_t type)
{
    if (PB_LTYPE(type) == PB_LTYPE_FIXED32) return PB_WT_32BIT;
    if (PB_LTYPE(type) == PB_LTYPE_FIXED64) return PB_WT_64BIT;
    return PB_WT_VARINT;
}

static bool skip_scalar(pb_istream_t *stream, pb_wire_type_t wire)
{
    uint64_t value;
    if (wire == PB_WT_VARINT) return pb_decode_varint(stream, &value);
    return pb_skip_field(stream, wire);
}

static ProtoResult validate_field(pb_istream_t *stream, pb_wire_type_t wire,
                                  const pb_field_iter_t *field, uint16_t *count,
                                  unsigned depth)
{
    pb_type_t type = PB_LTYPE(field->type);
    bool repeated = PB_HTYPE(field->type) == PB_HTYPE_REPEATED;
    unsigned cap = PROTO_MAX_ITEMS;
    if (repeated && PB_ATYPE(field->type) == PB_ATYPE_STATIC && field->array_size < cap)
        cap = field->array_size;
    if (type <= PB_LTYPE_LAST_PACKABLE) {
        pb_wire_type_t expected = scalar_wire(type);
        if (wire == PB_WT_STRING && repeated) {
            pb_istream_t packed;
            if (!pb_make_string_substream(stream, &packed)) return PROTO_MALFORMED;
            while (packed.bytes_left) {
                if (++*count > cap) return PROTO_LIMIT;
                if (!skip_scalar(&packed, expected)) return PROTO_MALFORMED;
            }
            return pb_close_string_substream(stream, &packed) ? PROTO_OK : PROTO_MALFORMED;
        }
        if (wire != expected) return PROTO_MALFORMED;
        if (repeated && ++*count > cap) return PROTO_LIMIT;
        if (!repeated) *count = 1;
        return skip_scalar(stream, wire) ? PROTO_OK : PROTO_MALFORMED;
    }
    if (wire != PB_WT_STRING) return PROTO_MALFORMED;
    if (repeated && ++*count > cap) return PROTO_LIMIT;
    if (!repeated) *count = 1;
    pb_istream_t sub;
    if (!pb_make_string_substream(stream, &sub)) return PROTO_MALFORMED;
    if (PB_LTYPE_IS_SUBMSG(type)) {
        ProtoResult result = validate_message(&sub, field->submsg_desc, depth + 1);
        if (result != PROTO_OK) return result;
    } else {
        size_t cap_bytes = type == PB_LTYPE_STRING ? PROTO_MAX_BLOB - 1 : PROTO_MAX_BLOB;
        if (PB_ATYPE(field->type) == PB_ATYPE_STATIC) {
            size_t fixed = field->data_size;
            if (type == PB_LTYPE_STRING) --fixed;
            if (type == PB_LTYPE_BYTES) fixed -= offsetof(pb_bytes_array_t, bytes);
            if (fixed < cap_bytes) cap_bytes = fixed;
        }
        if (sub.bytes_left > cap_bytes) return PROTO_LIMIT;
        if (type == PB_LTYPE_STRING) {
            char value[PROTO_MAX_BLOB];
            size_t length = sub.bytes_left;
            if (!pb_read(&sub, (pb_byte_t *)value, length)) return PROTO_MALFORMED;
            value[length] = '\0';
            if (memchr(value, 0, length) || !pb_validate_utf8(value)) return PROTO_MALFORMED;
        } else if (!pb_read(&sub, NULL, sub.bytes_left)) return PROTO_MALFORMED;
    }
    return pb_close_string_substream(stream, &sub) ? PROTO_OK : PROTO_MALFORMED;
}

static ProtoResult validate_message(pb_istream_t *stream, const pb_msgdesc_t *desc,
                                    unsigned depth)
{
    uint16_t counts[MAX_FIELDS] = {0};
    pb_field_iter_t field;
    if (depth > PROTO_MAX_DEPTH || desc->field_count > MAX_FIELDS) return PROTO_LIMIT;
    while (stream->bytes_left) {
        pb_wire_type_t wire;
        uint32_t tag;
        bool eof;
        if (!pb_decode_tag(stream, &wire, &tag, &eof) || tag == 0) return PROTO_MALFORMED;
        if (pb_field_iter_begin(&field, desc, NULL) && pb_field_iter_find(&field, tag)) {
            ProtoResult result = validate_field(stream, wire, &field, &counts[field.index], depth);
            if (result != PROTO_OK) return result;
        } else if (!skip_scalar(stream, wire)) {
            return PROTO_MALFORMED;
        }
    }
    if (pb_field_iter_begin(&field, desc, NULL)) {
        do {
            if (PB_HTYPE(field.type) == PB_HTYPE_REQUIRED && !counts[field.index])
                return PROTO_MALFORMED;
        } while (pb_field_iter_next(&field));
    }
    return PROTO_OK;
}

ProtoResult proto_validate(const pb_msgdesc_t *desc, const void *data, size_t size)
{
    if (!desc || (!data && size)) return PROTO_INVALID_ARGUMENT;
    if (size > PROTO_MAX_WIRE) return PROTO_LIMIT;
    pb_istream_t stream = pb_istream_from_buffer(data, size);
    return validate_message(&stream, desc, 1);
}

ProtoResult proto_decode(const pb_msgdesc_t *desc, const void *data, size_t size, void *message)
{
    if (!message) return PROTO_INVALID_ARGUMENT;
    ProtoResult result = proto_validate(desc, data, size);
    if (result != PROTO_OK) return result;
    pb_istream_t stream = pb_istream_from_buffer(data, size);
    return pb_decode(&stream, desc, message) ? PROTO_OK : PROTO_MALFORMED;
}

ProtoResult proto_encode(const pb_msgdesc_t *desc, const void *message,
                         void *data, size_t capacity, size_t *written)
{
    if (written) *written = 0;
    if (!desc || !message || !data || !written) return PROTO_INVALID_ARGUMENT;
    pb_ostream_t stream = pb_ostream_from_buffer(data, capacity);
    if (!pb_encode(&stream, desc, message)) return PROTO_LIMIT;
    ProtoResult result = proto_validate(desc, data, stream.bytes_written);
    if (result == PROTO_OK) *written = stream.bytes_written;
    return result;
}
