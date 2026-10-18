#include "storage_resolve.h"
#include "protocol/proto_codec.h"
#include "storage-resolve.pb.h"
#include "pb_decode.h"
#include <winhttp.h>
#include <string.h>
#include <wchar.h>

MediaResult storage_resolve_url(const wchar_t *origin, const MediaTrack *track, wchar_t output[HTTP_URL_CHARS])
{
    if (origin == NULL || track == NULL || output == NULL || wcsnlen_s(origin, 256) == 256 ||
        track->format < 0 || track->format > 2) { return MEDIA_INVALID; }
    char hex[41]; wchar_t file[41];
    if (media_id_to_hex(track->file_id, 20, hex, sizeof(hex)) != MEDIA_OK) { return MEDIA_INVALID; }
    for (size_t i = 0; i < 41; ++i) { file[i] = (wchar_t)hex[i]; }
    /* Native file constructor preserves legacy AudioFile.format unchanged.
     * See playback-resolution.md; this is not the stream-reporting enum. */
    return swprintf_s(output, HTTP_URL_CHARS,
        L"%ls/storage-resolve/v2/files/audio/interactive/%d/%ls?product=0", origin, track->format, file) > 0 ? MEDIA_OK : MEDIA_LIMIT;
}

typedef struct StorageDecode {
    MediaUrl urls[MEDIA_URLS];
    size_t count, seen;
    uint64_t utc, now;
    MediaResult result;
} StorageDecode;

static int nibble(char ch)
{
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}

static bool query_decode(const char *query, char output[MEDIA_URL_BYTES])
{
    size_t n = 0;
    while (*query != '\0') {
        unsigned char ch = (unsigned char)*query++;
        if (ch == '%') {
            if (query[0] == '\0' || query[1] == '\0') { return false; }
            int a = nibble(query[0]), b = nibble(query[1]);
            if (a < 0 || b < 0) { return false; }
            ch = (unsigned char)(a * 16 + b); query += 2;
        }
        if (ch == 0 || n + 1 >= MEDIA_URL_BYTES) { return false; }
        output[n++] = (char)ch;
    }
    output[n] = '\0'; return true;
}

static MediaResult timestamp(const char *value, char delimiter, uint64_t *expiry)
{
    uint64_t number = 0; const char *start = value;
    while (*value >= '0' && *value <= '9') {
        unsigned digit = (unsigned)(*value++ - '0');
        if (number > (UINT64_MAX / 1000 - digit) / 10) { return MEDIA_MALFORMED; }
        number = number * 10 + digit;
    }
    if (value == start || (*value != delimiter && *value != '\0')) { return MEDIA_MALFORMED; }
    *expiry = number * 1000; return MEDIA_OK;
}

static MediaResult expiry(const char *url, uint64_t *out)
{
    const char *query = strchr(url, '?');
    if (query == NULL) { return MEDIA_UNSUPPORTED; }
    ++query; bool found = false; uint64_t earliest = UINT64_MAX;
    while (*query != '\0') {
        /* Delimit raw components before decoding: an escaped '&' belongs to
         * its signed value and must never introduce another query field. */
        const char *next = strchr(query, '&');
        size_t bytes = next == NULL ? strlen(query) : (size_t)(next - query);
        if (bytes >= MEDIA_URL_BYTES) { return MEDIA_MALFORMED; }
        char encoded[MEDIA_URL_BYTES], part[MEDIA_URL_BYTES];
        memcpy(encoded, query, bytes); encoded[bytes] = '\0';
        if (!query_decode(encoded, part)) { return MEDIA_MALFORMED; }
        bool known = true; uint64_t candidate = UINT64_MAX;
        MediaResult result = MEDIA_OK;
        if (strncmp(part, "verify=", 7) == 0) { result = timestamp(part + 7, '-', &candidate); }
        else if (strncmp(part, "Expires=", 8) == 0) { result = timestamp(part + 8, '~', &candidate); }
        else if (strncmp(part, "__token__=", 10) == 0) {
            const char *token = part + 10; bool token_expiry = false;
            while (*token != '\0') {
                if (strncmp(token, "exp=", 4) == 0) {
                    uint64_t value;
                    result = timestamp(token + 4, '~', &value);
                    if (result != MEDIA_OK) { return result; }
                    if (value < candidate) { candidate = value; }
                    token_expiry = true;
                }
                const char *end = strchr(token, '~'); if (end == NULL) { break; } token = end + 1;
            }
            if (!token_expiry) { return MEDIA_MALFORMED; }
        } else if (part[0] >= '0' && part[0] <= '9' && strchr(part, '_') != NULL) {
            result = timestamp(part, '_', &candidate);
        } else { known = false; }
        if (result != MEDIA_OK) { return result; }
        if (known) {
            if (candidate < earliest) { earliest = candidate; }
            found = true;
        }
        if (next == NULL) { break; } query = next + 1;
    }
    if (!found) { return MEDIA_UNSUPPORTED; }
    *out = earliest; return MEDIA_OK;
}

static bool safe_url(const char *text)
{
    wchar_t wide[MEDIA_URL_BYTES]; size_t n = strlen(text);
    if (n == 0 || n >= MEDIA_URL_BYTES || strchr(text, '#') != NULL) { return false; }
    for (size_t i = 0; i <= n; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (i < n && (ch <= 32 || ch > 126 || ch == '\\')) { return false; }
        wide[i] = (wchar_t)ch;
    }
    URL_COMPONENTS u = {0}; u.dwStructSize = sizeof(u);
    u.dwHostNameLength = u.dwUserNameLength = u.dwPasswordLength = (DWORD)-1;
    return WinHttpCrackUrl(wide, 0, 0, &u) && u.nScheme == INTERNET_SCHEME_HTTPS &&
        u.dwHostNameLength != 0 && u.dwUserNameLength == 0 && u.dwPasswordLength == 0;
}

static bool decode_url(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
    (void)field; StorageDecode *ctx = *arg;
    if (++ctx->seen > MEDIA_URLS || stream->bytes_left >= MEDIA_URL_BYTES) { ctx->result = MEDIA_LIMIT; return false; }
    char url[MEDIA_URL_BYTES]; size_t length = stream->bytes_left;
    if (!pb_read(stream, (pb_byte_t *)url, length)) { return false; }
    url[length] = '\0';
    if (memchr(url, 0, length) != NULL || !safe_url(url)) { ctx->result = MEDIA_MALFORMED; return false; }
    uint64_t until = 0;
    MediaResult result = expiry(url, &until);
    uint64_t lease = 60000; /* Finite fallback and a cap even for long signed leases. */
    if (result == MEDIA_OK) {
        if (until <= ctx->utc || until - ctx->utc <= 5000) { return true; }
        uint64_t remaining = until - ctx->utc - 5000;
        if (remaining < lease) { lease = remaining; }
    } else if (result != MEDIA_UNSUPPORTED) { ctx->result = result; return false; }
    MediaUrl *out = &ctx->urls[ctx->count++];
    memcpy(out->url, url, length + 1); out->refresh_at = core_deadline_after(ctx->now, lease);
    return true;
}

MediaResult storage_resolve_decode(const void *data, size_t bytes, const uint8_t file_id[20],
    uint64_t utc_ms, uint64_t now_ms, MediaUrl output[MEDIA_URLS], size_t *count)
{
    if (data == NULL || file_id == NULL || output == NULL || count == NULL) { return MEDIA_INVALID; }
    if (bytes > MEDIA_RESPONSE_BYTES) { return MEDIA_LIMIT; }
    ProtoResult valid = proto_validate(spotify_download_proto_StorageResolveResponse_fields, data, bytes);
    if (valid != PROTO_OK) { return valid == PROTO_LIMIT ? MEDIA_LIMIT : MEDIA_MALFORMED; }
    StorageDecode ctx = {0}; ctx.utc = utc_ms; ctx.now = now_ms;
    spotify_download_proto_StorageResolveResponse response = spotify_download_proto_StorageResolveResponse_init_zero;
    response.cdnurl.funcs.decode = decode_url; response.cdnurl.arg = &ctx;
    pb_istream_t stream = pb_istream_from_buffer(data, bytes);
    if (!pb_decode(&stream, spotify_download_proto_StorageResolveResponse_fields, &response)) {
        return ctx.result == MEDIA_OK ? MEDIA_MALFORMED : ctx.result;
    }
    if (response.result == spotify_download_proto_StorageResolveResponse_Result_RESTRICTED) { return MEDIA_RESTRICTED; }
    if (response.result == spotify_download_proto_StorageResolveResponse_Result_STORAGE) { return MEDIA_STORAGE; }
    if (response.result != spotify_download_proto_StorageResolveResponse_Result_CDN) { return MEDIA_UNSUPPORTED; }
    if (response.fileid.size != 0 && (response.fileid.size != 20 || memcmp(response.fileid.bytes, file_id, 20) != 0)) {
        return MEDIA_MALFORMED;
    }
    if (ctx.seen == 0) { return MEDIA_MALFORMED; }
    if (ctx.count == 0) { return MEDIA_EXPIRED; }
    memcpy(output, ctx.urls, sizeof(ctx.urls)); *count = ctx.count; return MEDIA_OK;
}
