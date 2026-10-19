#include "lyrics_internal.h"
#include "protocol/media_source.h"
#include <string.h>
#include <wchar.h>

bool lyrics_fixture_origin_valid(const wchar_t *origin)
{
    if (origin == NULL || wcsncmp(origin, L"http://127.0.0.1:", 17) != 0) { return false; }
    const wchar_t *p = origin + 17; unsigned port = 0; size_t digits = 0;
    while (*p >= L'0' && *p <= L'9') {
        if (++digits > 5) { return false; }
        port = port * 10 + (unsigned)(*p++ - L'0');
    }
    return digits != 0 && port != 0 && port <= 65535 && *p == 0;
}

static bool text(JsonString value, size_t limit, bool required)
{
    if (value.bytes == 0) { return !required; }
    return value.data != NULL && value.bytes <= limit && memchr(value.data, 0, value.bytes) == NULL &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data, (int)value.bytes, NULL, 0) > 0;
}

static bool append(char output[HTTP_URL_CHARS], size_t *used, const char *text_value, size_t bytes)
{
    if (bytes >= HTTP_URL_CHARS - *used) { return false; }
    memcpy(output + *used, text_value, bytes); *used += bytes; output[*used] = 0; return true;
}
static bool encode(char output[HTTP_URL_CHARS], size_t *used, JsonString value)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < value.bytes; ++i) {
        unsigned char ch = (unsigned char)value.data[i];
        bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || (ch != 0 && strchr("-_.!~*'()", ch) != NULL);
        char encoded[3] = {'%', hex[ch >> 4], hex[ch & 15]};
        if (!append(output, used, safe ? (const char *)&value.data[i] : encoded, safe ? 1 : 3)) { return false; }
    }
    return true;
}

LyricsResult lyrics_build_url(const LyricsConfig *config, const LyricsTrack *track,
    uint8_t id[16], wchar_t output[HTTP_URL_CHARS])
{
    const ApiContract *contract; char uri[MEDIA_URI_BYTES];
    if (config == NULL || track == NULL || output == NULL || id == NULL ||
        !text(track->uri, MEDIA_URI_BYTES - 1, true) || !text(track->artwork, 512, true) ||
        !text(track->client_language, 64, false) || !text(track->market, 16, false) ||
        json_base_string_copy(track->uri, uri, sizeof(uri)) != JSON_OK || media_gid_from_uri(uri, id) != MEDIA_OK) {
        return LYRICS_INVALID;
    }
    if (contract_lookup(API_LYRICS, &contract) != API_CONTRACT_OK ||
        contract->transport != API_HTTP_JSON || contract->method != API_METHOD_GET || contract->persisted_sha256 != NULL) {
        return LYRICS_INVALID;
    }
    char url[HTTP_URL_CHARS] = {0}; size_t used = 0; const char *p = contract->url;
    if (config->fixture_origin != NULL) {
        if (!lyrics_fixture_origin_valid(config->fixture_origin)) { return LYRICS_INVALID; }
        for (const wchar_t *w = config->fixture_origin; *w != 0; ++w) {
            char ch = (char)*w; if (!append(url, &used, &ch, 1)) { return LYRICS_TOO_LARGE; }
        }
        const char *scheme = strstr(p, "://"); p = scheme == NULL ? NULL : strchr(scheme + 3, '/');
        if (p == NULL) { return LYRICS_INVALID; }
    }
    while (*p != 0) {
        if (strncmp(p, "{trackId}", 9) == 0) {
            if (!encode(url, &used, (JsonString){uri + 14, 22})) { return LYRICS_TOO_LARGE; } p += 9;
        } else if (strncmp(p, "{image}", 7) == 0) {
            if (!encode(url, &used, track->artwork)) { return LYRICS_TOO_LARGE; } p += 7;
        } else if (!append(url, &used, p++, 1)) { return LYRICS_TOO_LARGE; }
    }
    const char *query = track->vocal_removal ? "?format=json&vocalRemoval=true" : "?format=json&vocalRemoval=false";
    if (!append(url, &used, query, strlen(query))) { return LYRICS_TOO_LARGE; }
    if (track->client_language.bytes != 0 && (!append(url, &used, "&clientLanguage=", 16) ||
        !encode(url, &used, track->client_language))) { return LYRICS_TOO_LARGE; }
    if (track->market.bytes != 0 && (!append(url, &used, "&market=", 8) || !encode(url, &used, track->market))) { return LYRICS_TOO_LARGE; }
    for (size_t i = 0; i <= used; ++i) { output[i] = (wchar_t)(unsigned char)url[i]; }
    return LYRICS_OK;
}
