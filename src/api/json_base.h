#ifndef SPOTIC_API_JSON_BASE_H
#define SPOTIC_API_JSON_BASE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "yyjson.h"

#define JSON_BASE_INPUT_LIMIT ((size_t)16 * 1024)
#define JSON_BASE_VALUE_LIMIT ((size_t)4096)
typedef enum JsonResult {
    JSON_OK, JSON_INVALID, JSON_TOO_LARGE, JSON_NO_MEMORY, JSON_MISSING,
    JSON_NULL, JSON_TYPE, JSON_RANGE
} JsonResult;
typedef struct JsonBase { yyjson_doc *doc; } JsonBase;
typedef struct JsonString { const char *data; size_t bytes; } JsonString;

/* Start at {0}; close before reuse. Caller owns input and allocator pool; both
 * remain exclusively borrowed through close. Input is modified in situ and
 * must include YYJSON_PADDING_SIZE writable bytes after its logical length.
 * Input capacity + pool bytes must fit the existing 256 KiB protocol scratch
 * allowance (shared with other protocol scratch, never a new hidden pool).
 * Strict UTF-8/JSON, no malloc fallback, bounded values/input. Failed parses
 * may also modify input. No Page model or retained global allocator. */
JsonResult json_base_parse(JsonBase *json, char *input, size_t bytes, size_t capacity,
    void *pool, size_t pool_bytes);
void json_base_close(JsonBase *json);
yyjson_val *json_base_root(const JsonBase *json);
JsonResult json_base_field(yyjson_val *object, const char *key, yyjson_val **output);
/* Readers distinguish null, missing, wrong type and out-of-range values.
 * Strings (including embedded NUL) borrow the padded input until close/reuse.
 * Never free borrowed values/strings or pass a string to strlen. */
JsonResult json_base_string(yyjson_val *value, size_t max_bytes, JsonString *output);
JsonResult json_base_u64(yyjson_val *value, uint64_t maximum, uint64_t *output);
JsonResult json_base_bool(yyjson_val *value, bool *output);
/* Explicit owned C-string copy; rejects embedded NUL and insufficient space. */
JsonResult json_base_string_copy(JsonString string, char *output, size_t capacity);
#endif
