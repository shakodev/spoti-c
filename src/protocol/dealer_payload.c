#include "dealer_payload.h"
#include <string.h>

static int digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') { return c - 'A'; }
    if (c >= 'a' && c <= 'z') { return c - 'a' + 26; }
    if (c >= '0' && c <= '9') { return c - '0' + 52; }
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}
DealerResult dealer_base64(const char *in, size_t n, void *output, size_t cap, size_t *written)
{
    if (!in || !output || !written || n % 4) { return DEALER_MALFORMED; }
    size_t at = 0; unsigned char *out = output;
    for (size_t i = 0; i < n; i += 4) {
        int a = digit((unsigned char)in[i]), b = digit((unsigned char)in[i + 1]);
        int c = digit((unsigned char)in[i + 2]), d = digit((unsigned char)in[i + 3]);
        bool p = in[i + 2] == '=', q = in[i + 3] == '=';
        if (a < 0 || b < 0 || (c < 0 && !p) || (d < 0 && !q) || (p && !q) ||
            ((p || q) && i + 4 != n) || (p && (b & 15)) || (!p && q && (c & 3))) { return DEALER_MALFORMED; }
        size_t count = p ? 1 : q ? 2 : 3;
        if (count > cap - at) { return DEALER_LIMIT; }
        out[at++] = (unsigned char)((a << 2) | (b >> 4));
        if (!p) { out[at++] = (unsigned char)((b << 4) | (c >> 2)); }
        if (!q) { out[at++] = (unsigned char)((c << 6) | d); }
    }
    *written = at; return DEALER_OK;
}
uint32_t dealer_crc32(const void *data, size_t n)
{
    const unsigned char *p = data; uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) { crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
    }
    return ~crc;
}
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool unique_keys(yyjson_val *object)
{
    if (!yyjson_is_obj(object)) { return true; }
    yyjson_obj_iter outer = yyjson_obj_iter_with(object); yyjson_val *key;
    size_t at = 0;
    while ((key = yyjson_obj_iter_next(&outer)) != NULL) {
        yyjson_obj_iter inner = yyjson_obj_iter_with(object);
        for (size_t i = 0; i < at; ++i) {
            yyjson_val *other = yyjson_obj_iter_next(&inner);
            if (yyjson_get_len(key) == yyjson_get_len(other) &&
                !memcmp(yyjson_get_str(key), yyjson_get_str(other), yyjson_get_len(key))) { return false; }
        }
        ++at;
    }
    return true;
}
DealerResult dealer_gzip(tinfl_decompressor *state, const void *input, size_t n,
    void *output, size_t cap, size_t *written)
{
    if (!state || !input || !output || !written || cap > DEALER_PAYLOAD_CAP) { return DEALER_INVALID; }
    const unsigned char *p = input; size_t at = 10;
    if (n < 18 || p[0] != 31 || p[1] != 139 || p[2] != 8 || (p[3] & 0xe0)) { return DEALER_MALFORMED; }
    if (p[3] & 4) {
        if (at + 2 > n - 8) { return DEALER_MALFORMED; }
        size_t extra = (size_t)p[at] | (size_t)p[at + 1] << 8; at += 2;
        if (extra > n - 8 - at) { return DEALER_MALFORMED; } at += extra;
    }
    for (unsigned flag = 8; flag <= 16; flag <<= 1) {
        if (p[3] & flag) {
            while (at < n - 8 && p[at]) { ++at; }
            if (at == n - 8) { return DEALER_MALFORMED; } ++at;
        }
    }
    if (p[3] & 2) {
        if (at + 2 > n - 8 || (dealer_crc32(p, at) & 65535) != ((uint32_t)p[at] | (uint32_t)p[at + 1] << 8)) {
            return DEALER_MALFORMED;
        } at += 2;
    }
    if (le32(p + n - 4) > cap) { return DEALER_LIMIT; }
    size_t in_bytes = n - 8 - at, out_bytes = cap;
    tinfl_init(state);
    tinfl_status status = tinfl_decompress(state, p + at, &in_bytes, output, output, &out_bytes,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (status == TINFL_STATUS_HAS_MORE_OUTPUT) { return DEALER_LIMIT; }
    if (status != TINFL_STATUS_DONE || in_bytes != n - 8 - at || out_bytes != le32(p + n - 4) ||
        dealer_crc32(output, out_bytes) != le32(p + n - 8)) { return DEALER_MALFORMED; }
    *written = out_bytes; return DEALER_OK;
}
yyjson_doc *dealer_json(char *text, size_t n, void *pool, size_t cap)
{
    if (!text || !pool || !n || n > DEALER_PAYLOAD_CAP) { return NULL; }
    yyjson_alc alc;
    if (!yyjson_alc_pool_init(&alc, pool, cap)) { return NULL; }
    memset(text + n, 0, YYJSON_PADDING_SIZE);
    yyjson_doc *doc = yyjson_read_opts(text, n, YYJSON_READ_INSITU, &alc, NULL);
    if (!doc) { return NULL; }
    if (yyjson_doc_get_val_count(doc) > 4096) { yyjson_doc_free(doc); return NULL; }
    /* Iterative tree depth/value bound before application traversal. */
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *stack[17]; size_t indexes[17] = {0}; unsigned depth = 0;
    stack[0] = root;
    for (;;) {
        yyjson_val *v = stack[depth]; size_t count = yyjson_is_obj(v) ? yyjson_obj_size(v) * 2 : yyjson_arr_size(v);
        if (!indexes[depth] && !unique_keys(v)) { yyjson_doc_free(doc); return NULL; }
        if (indexes[depth] == count) { if (!depth) { break; } --depth; continue; }
        yyjson_val *child;
        if (yyjson_is_arr(v)) { child = yyjson_arr_get(v, indexes[depth]++); }
        else {
            /* Object values are traversed with yyjson's public iterator. */
            yyjson_obj_iter iter = yyjson_obj_iter_with(v); child = NULL;
            size_t wanted = indexes[depth]++ / 2;
            for (size_t i = 0; i <= wanted; ++i) { child = yyjson_obj_iter_next(&iter); }
            if ((indexes[depth] - 1) & 1) { child = yyjson_obj_iter_get_val(child); }
        }
        if (yyjson_is_ctn(child)) {
            if (depth == 16) { yyjson_doc_free(doc); return NULL; }
            stack[++depth] = child; indexes[depth] = 0;
        }
    }
    return doc;
}
bool dealer_string(yyjson_val *v, char *out, size_t cap)
{
    if (!yyjson_is_str(v)) { return false; }
    size_t n = yyjson_get_len(v); const char *p = yyjson_get_str(v);
    if (n >= cap || memchr(p, 0, n)) { return false; }
    memcpy(out, p, n); out[n] = 0; return true;
}
bool dealer_u32(yyjson_val *v, uint32_t max, uint32_t *out)
{
    if (!yyjson_is_uint(v) || yyjson_get_uint(v) > max) { return false; }
    *out = (uint32_t)yyjson_get_uint(v); return true;
}
DealerResult dealer_envelope(unsigned char *wire, size_t n, void *scratch,
    tinfl_decompressor *inflate, DealerEnvelope *out)
{
    DealerEnvelope e = {0}; DealerResult result = DEALER_MALFORMED;
    yyjson_doc *doc = dealer_json((char *)wire, n, scratch, DEALER_PAYLOAD_CAP);
    if (!doc) { return result; }
    yyjson_val *root = yyjson_doc_get_root(doc), *headers = yyjson_obj_get(root, "headers");
    char type[16], encoding[32] = "";
    if (!dealer_string(yyjson_obj_get(root, "type"), type, sizeof(type))) { goto done; }
    e.request = !strcmp(type, "request");
    if (!e.request && strcmp(type, "message")) { goto done; }
    if (headers && !yyjson_is_obj(headers)) { goto done; }
    yyjson_val *value = yyjson_obj_get(headers, "Transfer-Encoding");
    if (value && (!dealer_string(value, encoding, sizeof(encoding)) || strcmp(encoding, "gzip"))) { goto done; }
    e.gzip = *encoding != 0;
    value = yyjson_obj_get(headers, "Spotify-Connection-Id");
    if (value && !dealer_string(value, e.connection_id, sizeof(e.connection_id))) { goto done; }
    if (e.request) {
        if (!dealer_string(yyjson_obj_get(root, "message_ident"), e.uri, sizeof(e.uri)) ||
            !dealer_string(yyjson_obj_get(root, "key"), e.key, sizeof(e.key)) || !*e.key) { goto done; }
        value = yyjson_obj_get(yyjson_obj_get(root, "payload"), "compressed");
    } else {
        if (!dealer_string(yyjson_obj_get(root, "uri"), e.uri, sizeof(e.uri))) { goto done; }
        value = yyjson_obj_get(root, "payloads");
        if (!value || (yyjson_is_arr(value) && !yyjson_arr_size(value))) { result = DEALER_OK; goto done; }
        if (!yyjson_is_arr(value) || yyjson_arr_size(value) != 1) { goto done; }
        value = yyjson_arr_get_first(value);
    }
    if (!yyjson_is_str(value)) { goto done; }
    e.payload = (unsigned char *)yyjson_get_str(value);
    result = dealer_base64((char *)e.payload, yyjson_get_len(value), e.payload, DEALER_PAYLOAD_CAP, &e.bytes);
done:
    yyjson_doc_free(doc);
    if (result != DEALER_OK) { return result; }
    if (e.gzip) {
        result = dealer_gzip(inflate, e.payload, e.bytes, scratch, DEALER_PAYLOAD_CAP, &e.bytes);
        if (result != DEALER_OK) { return result; } e.payload = scratch;
    }
    if (e.bytes) { memmove(wire, e.payload, e.bytes); } e.payload = wire;
    memset(wire + e.bytes, 0, YYJSON_PADDING_SIZE); *out = e; return DEALER_OK;
}
