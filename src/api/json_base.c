#include "json_base.h"
#include "core/memory_budget.h"
#include <string.h>

JsonResult json_base_parse(JsonBase *json, char *input, size_t bytes, size_t capacity,
    void *pool, size_t pool_bytes)
{
    yyjson_alc allocator;
    yyjson_read_err error;
    if (json == NULL || json->doc != NULL || input == NULL || pool == NULL ||
        bytes > capacity || capacity - bytes < YYJSON_PADDING_SIZE) { return JSON_INVALID; }
    if (bytes > JSON_BASE_INPUT_LIMIT || capacity > MEMORY_BUDGET_PROTOCOL ||
        pool_bytes > MEMORY_BUDGET_PROTOCOL - capacity) { return JSON_TOO_LARGE; }
    /* The buffers must be disjoint: the DOM cannot overwrite borrowed input. */
    uintptr_t a = (uintptr_t)input, b = (uintptr_t)pool;
    if (capacity > UINTPTR_MAX - a || pool_bytes > UINTPTR_MAX - b ||
        (a < b + pool_bytes && b < a + capacity)) { return JSON_INVALID; }
    if (!yyjson_alc_pool_init(&allocator, pool, pool_bytes)) { return JSON_NO_MEMORY; }
    memset(input + bytes, 0, YYJSON_PADDING_SIZE);
    json->doc = yyjson_read_opts(input, bytes, YYJSON_READ_INSITU, &allocator, &error);
    if (json->doc == NULL) {
        return error.code == YYJSON_READ_ERROR_MEMORY_ALLOCATION ? JSON_NO_MEMORY : JSON_INVALID;
    }
    if (yyjson_doc_get_val_count(json->doc) > JSON_BASE_VALUE_LIMIT) {
        json_base_close(json); return JSON_TOO_LARGE;
    }
    return JSON_OK;
}

void json_base_close(JsonBase *json)
{
    if (json != NULL && json->doc != NULL) { yyjson_doc_free(json->doc); json->doc = NULL; }
}

yyjson_val *json_base_root(const JsonBase *json)
{
    return json == NULL ? NULL : yyjson_doc_get_root(json->doc);
}

static JsonResult present(yyjson_val *value)
{
    if (value == NULL) { return JSON_MISSING; }
    return yyjson_is_null(value) ? JSON_NULL : JSON_OK;
}

JsonResult json_base_field(yyjson_val *object, const char *key, yyjson_val **output)
{
    if (output == NULL || key == NULL) { return JSON_INVALID; }
    *output = NULL;
    JsonResult result = present(object);
    if (result != JSON_OK) { return result; }
    if (!yyjson_is_obj(object)) { return JSON_TYPE; }
    *output = yyjson_obj_get(object, key);
    return present(*output);
}

JsonResult json_base_string(yyjson_val *value, size_t max_bytes, JsonString *output)
{
    if (output == NULL) { return JSON_INVALID; }
    *output = (JsonString){0};
    JsonResult result = present(value);
    if (result != JSON_OK) { return result; }
    if (!yyjson_is_str(value)) { return JSON_TYPE; }
    size_t bytes = yyjson_get_len(value);
    if (bytes > max_bytes) { return JSON_RANGE; }
    *output = (JsonString){yyjson_get_str(value), bytes}; return JSON_OK;
}

JsonResult json_base_u64(yyjson_val *value, uint64_t maximum, uint64_t *output)
{
    if (output == NULL) { return JSON_INVALID; }
    *output = 0;
    JsonResult result = present(value);
    if (result != JSON_OK) { return result; }
    if (!yyjson_is_uint(value)) { return JSON_TYPE; }
    uint64_t number = yyjson_get_uint(value);
    if (number > maximum) { return JSON_RANGE; }
    *output = number; return JSON_OK;
}

JsonResult json_base_bool(yyjson_val *value, bool *output)
{
    if (output == NULL) { return JSON_INVALID; }
    *output = false;
    JsonResult result = present(value);
    if (result != JSON_OK) { return result; }
    if (!yyjson_is_bool(value)) { return JSON_TYPE; }
    *output = yyjson_get_bool(value); return JSON_OK;
}

JsonResult json_base_string_copy(JsonString string, char *output, size_t capacity)
{
    if (string.data == NULL || output == NULL) { return JSON_INVALID; }
    if (string.bytes >= capacity) { return JSON_RANGE; }
    if (memchr(string.data, '\0', string.bytes) != NULL) { return JSON_INVALID; }
    memmove(output, string.data, string.bytes); output[string.bytes] = '\0';
    return JSON_OK;
}
