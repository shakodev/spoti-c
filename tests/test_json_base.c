#include "api/json_base.h"
#include "core/memory_budget.h"
#include "support/test_check.h"
#include <string.h>

static char input[JSON_BASE_INPUT_LIMIT + YYJSON_PADDING_SIZE];
static _Alignas(16) unsigned char pool[192 * 1024];

static JsonResult parse(JsonBase *json, const char *text)
{
    size_t n = strlen(text);
    memcpy(input, text, n);
    return json_base_parse(json, input, n, sizeof(input), pool, sizeof(pool));
}

static void readers(void)
{
    JsonBase json = {0};
    CHECK(parse(&json, "{\"str\":\"a\\n\\u20ac\\ud83d\\ude00\",\"n\":42,\"b\":true,\"null\":null,"
        "\"max\":18446744073709551615,\"neg\":-1,\"real\":1.5,\"nul\":\"a\\u0000b\"}") == JSON_OK);
    yyjson_val *root = json_base_root(&json), *v = NULL;
    CHECK(json_base_field(root, "str", &v) == JSON_OK);
    JsonString string;
    CHECK(json_base_string(v, 9, &string) == JSON_OK);
    CHECK(string.bytes == 9 && memcmp(string.data, "a\n\xe2\x82\xac\xf0\x9f\x98\x80", 9) == 0);
    CHECK(string.data >= input && string.data + string.bytes < input + sizeof(input));
    char owned[16];
    CHECK(json_base_string_copy(string, owned, 9) == JSON_RANGE);
    CHECK(json_base_string_copy(string, owned, sizeof(owned)) == JSON_OK);
    CHECK(json_base_string(v, 8, &string) == JSON_RANGE && string.data == NULL);
    CHECK(json_base_field(root, "missing", &v) == JSON_MISSING && v == NULL);
    CHECK(json_base_string(v, 20, &string) == JSON_MISSING);
    CHECK(json_base_field(root, "null", &v) == JSON_NULL && v != NULL);
    uint64_t number;
    bool flag;
    CHECK(json_base_string(v, 20, &string) == JSON_NULL);
    CHECK(json_base_u64(v, 100, &number) == JSON_NULL && number == 0);
    CHECK(json_base_bool(v, &flag) == JSON_NULL && !flag);
    CHECK(json_base_field(root, "n", &v) == JSON_OK);
    CHECK(json_base_u64(v, 42, &number) == JSON_OK && number == 42);
    CHECK(json_base_u64(v, 41, &number) == JSON_RANGE && number == 0);
    CHECK(json_base_string(v, 20, &string) == JSON_TYPE);
    CHECK(json_base_bool(v, &flag) == JSON_TYPE);
    CHECK(json_base_field(v, "x", &root) == JSON_TYPE && root == NULL);
    root = json_base_root(&json);
    CHECK(json_base_field(root, "b", &v) == JSON_OK);
    CHECK(json_base_bool(v, &flag) == JSON_OK && flag);
    CHECK(json_base_field(root, "max", &v) == JSON_OK);
    CHECK(json_base_u64(v, UINT64_MAX, &number) == JSON_OK && number == UINT64_MAX);
    CHECK(json_base_field(root, "neg", &v) == JSON_OK);
    CHECK(json_base_u64(v, UINT64_MAX, &number) == JSON_TYPE);
    CHECK(json_base_field(root, "real", &v) == JSON_OK);
    CHECK(json_base_u64(v, UINT64_MAX, &number) == JSON_TYPE);
    CHECK(json_base_field(root, "nul", &v) == JSON_OK);
    CHECK(json_base_string(v, 3, &string) == JSON_OK && string.bytes == 3);
    CHECK(json_base_string_copy(string, owned, sizeof(owned)) == JSON_INVALID);
    CHECK(json_base_parse(&json, input, 1, sizeof(input), pool, sizeof(pool)) == JSON_INVALID);
    json_base_close(&json); json_base_close(&json);
    CHECK(json_base_root(&json) == NULL);
    /* Only the explicit copy survives input/pool reuse. */
    CHECK(parse(&json, "[]") == JSON_OK && owned[0] == 'a' && owned[1] == '\n');
    json_base_close(&json);
}

static void malformed_and_limits(void)
{
    static const char *const invalid[] = {"", "{", "[1,]", "{\"a\":01}", "NaN", "Infinity",
        "1e", "1e9999", "--1", "{\"x\":1} trailing", "\"\\ud800\"", "\"\\udc00\"",
        "\"\xc0\x80\"", "\"\xed\xa0\x80\"", "\"\xf4\x90\x80\x80\"", "\"\x80\"", "/*x*/null"};
    JsonBase json = {0};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(parse(&json, invalid[i]) == JSON_INVALID && json.doc == NULL);
    }
    memcpy(input, "{\"x\":1}", 7);
    CHECK(json_base_parse(&json, input, 7, 7, pool, sizeof(pool)) == JSON_INVALID);
    CHECK(json_base_parse(&json, input, JSON_BASE_INPUT_LIMIT + 1, sizeof(input), pool, sizeof(pool)) == JSON_INVALID);
    CHECK(json_base_parse(&json, input, 7, sizeof(input), input, 200) == JSON_INVALID);
    CHECK(json_base_parse(&json, input, 7, sizeof(input), pool, MEMORY_BUDGET_PROTOCOL) == JSON_TOO_LARGE);
    CHECK(json_base_parse(&json, input, 7, sizeof(input), pool, 32) == JSON_NO_MEMORY);
    CHECK(json_base_parse(&json, input, 7, sizeof(input), pool, 64) == JSON_NO_MEMORY);
    /* A valid JSON fitting the byte cap but exceeding the independent DOM cap. */
    input[0] = '[';
    for (size_t i = 0; i < JSON_BASE_VALUE_LIMIT; ++i) { input[1 + i * 2] = '0'; input[2 + i * 2] = ','; }
    input[JSON_BASE_VALUE_LIMIT * 2] = ']';
    CHECK(json_base_parse(&json, input, JSON_BASE_VALUE_LIMIT * 2 + 1,
        sizeof(input), pool, sizeof(pool)) == JSON_TOO_LARGE && json.doc == NULL);
    /* Exactly the input cap, mostly whitespace, is valid. */
    memset(input, ' ', JSON_BASE_INPUT_LIMIT); memcpy(input, "null", 4);
    CHECK(json_base_parse(&json, input, JSON_BASE_INPUT_LIMIT, sizeof(input), pool, sizeof(pool)) == JSON_OK);
    json_base_close(&json);
    static char oversized[JSON_BASE_INPUT_LIMIT + 5];
    CHECK(json_base_parse(&json, oversized, JSON_BASE_INPUT_LIMIT + 1,
        sizeof(oversized), pool, sizeof(pool)) == JSON_TOO_LARGE);
}

int main(void)
{
    readers(); malformed_and_limits();
    puts("Bounded in-situ JSON: UTF-8, types, numbers, ownership and pool exhaustion passed");
    return 0;
}
