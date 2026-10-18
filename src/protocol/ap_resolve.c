#include "ap_resolve.h"
#include "api/json_base.h"
#include <string.h>

bool ap_endpoint_valid(const ApEndpoint *endpoint)
{
    if (endpoint == NULL || endpoint->port == 0) { return false; }
    size_t length = 0, label = 0;
    while (length < AP_HOST_CHARS && endpoint->host[length] != '\0') {
        char c = endpoint->host[length];
        if (c == '.') {
            if (label == 0 || endpoint->host[length - 1] == '-') { return false; }
            label = 0;
        } else {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '-') || (label == 0 && c == '-') || ++label > 63) {
                return false;
            }
        }
        ++length;
    }
    return length != 0 && length < AP_HOST_CHARS && label != 0 && endpoint->host[length - 1] != '-';
}

static ApResult parse_array(yyjson_val *root, const char *name, ApEndpoint *output, size_t *count)
{
    yyjson_val *array;
    if (json_base_field(root, name, &array) != JSON_OK || !yyjson_is_arr(array)) { return AP_MALFORMED; }
    size_t n = yyjson_arr_size(array);
    if (n == 0 || n > AP_ENDPOINT_COUNT) { return AP_LIMIT; }
    for (size_t i = 0; i < n; ++i) {
        JsonString text;
        if (json_base_string(yyjson_arr_get(array, i), AP_HOST_CHARS + 6, &text) != JSON_OK ||
            text.bytes == 0 || memchr(text.data, '\0', text.bytes) != NULL) { return AP_MALFORMED; }
        size_t colon = text.bytes;
        for (size_t p = 0; p < text.bytes; ++p) { if (text.data[p] == ':') { colon = p; break; } }
        if (colon == 0 || colon >= AP_HOST_CHARS || colon + 1 >= text.bytes) { return AP_MALFORMED; }
        unsigned port = 0;
        for (size_t p = colon + 1; p < text.bytes; ++p) {
            unsigned char c = (unsigned char)text.data[p];
            if (c < '0' || c > '9' || port > (65535u - (unsigned)(c - '0')) / 10) { return AP_MALFORMED; }
            port = port * 10 + (unsigned)(c - '0');
        }
        memcpy(output[i].host, text.data, colon); output[i].host[colon] = '\0'; output[i].port = (uint16_t)port;
        if (!ap_endpoint_valid(&output[i])) { return AP_MALFORMED; }
    }
    *count = n; return AP_OK;
}

ApResult ap_resolve_parse(char *input, size_t bytes, size_t capacity, void *pool,
    size_t pool_bytes, ApEndpoints *output)
{
    if (output == NULL) { return AP_INVALID; }
    JsonBase json = {0};
    JsonResult parsed = json_base_parse(&json, input, bytes, capacity, pool, pool_bytes);
    if (parsed != JSON_OK) {
        return parsed == JSON_NO_MEMORY ? AP_NO_MEMORY : parsed == JSON_TOO_LARGE ? AP_LIMIT : AP_MALFORMED;
    }
    ApEndpoints endpoints = {0};
    yyjson_val *root = json_base_root(&json);
    ApResult result = parse_array(root, "accesspoint", endpoints.accesspoint, &endpoints.accesspoint_count);
    if (result == AP_OK) { result = parse_array(root, "dealer", endpoints.dealer, &endpoints.dealer_count); }
    if (result == AP_OK) { result = parse_array(root, "spclient", endpoints.spclient, &endpoints.spclient_count); }
    json_base_close(&json);
    if (result == AP_OK) { *output = endpoints; }
    return result;
}

ApResult ap_resolve_start(HttpClient *http, void *body, size_t capacity, HttpRequestId *id)
{
    /* REA service observation and pinned reference: docs/specs/ap-session.md. */
    return ap_resolve_start_at(http,
        L"https://apresolve.spotify.com/?type=accesspoint&type=dealer&type=spclient",
        body, capacity, id);
}

ApResult ap_resolve_start_at(HttpClient *http, const wchar_t *url, void *body,
    size_t capacity, HttpRequestId *id)
{
    if (http == NULL || url == NULL || body == NULL || id == NULL || capacity == 0 ||
        capacity > JSON_BASE_INPUT_LIMIT) { return AP_INVALID; }
    HttpRequest request = {.url = url, .method = HTTP_GET, .body = body,
        .body_capacity = capacity, .body_limit = capacity, .timeout_ms = 10000, .max_retries = 1};
    HttpResult result = http_submit(http, &request, id);
    return result == HTTP_OK ? AP_OK : result == HTTP_FULL ? AP_FULL :
        result == HTTP_NO_MEMORY ? AP_NO_MEMORY : AP_IO_ERROR;
}
