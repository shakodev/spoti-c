#include "media_source.h"
#include <string.h>

MediaResult media_gid_from_base62(const char *text, uint8_t output[16])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (text == NULL || output == NULL || strlen(text) != 22) { return MEDIA_INVALID; }
    uint8_t value[16] = {0};
    for (size_t i = 0; i < 22; ++i) {
        const char *digit = strchr(alphabet, text[i]);
        if (digit == NULL) { return MEDIA_INVALID; }
        unsigned carry = (unsigned)(digit - alphabet);
        for (size_t j = 16; j != 0; --j) {
            unsigned n = value[j - 1] * 62u + carry;
            value[j - 1] = (uint8_t)n; carry = n >> 8;
        }
        if (carry != 0) { return MEDIA_LIMIT; }
    }
    memcpy(output, value, sizeof(value)); return MEDIA_OK;
}

MediaResult media_gid_from_uri(const char *uri, uint8_t output[16])
{
    if (uri == NULL || strncmp(uri, "spotify:track:", 14) != 0) { return MEDIA_INVALID; }
    return media_gid_from_base62(uri + 14, output);
}

MediaResult media_gid_to_uri(const uint8_t gid[16], char output[MEDIA_URI_BYTES])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (gid == NULL || output == NULL) { return MEDIA_INVALID; }
    uint8_t value[16]; memcpy(value, gid, sizeof(value));
    memcpy(output, "spotify:track:", 14); output[36] = '\0';
    for (size_t i = 36; i != 14; --i) {
        unsigned carry = 0;
        for (size_t j = 0; j < 16; ++j) {
            unsigned n = carry * 256u + value[j]; value[j] = (uint8_t)(n / 62); carry = n % 62;
        }
        output[i - 1] = alphabet[carry];
    }
    return MEDIA_OK;
}

static int hex(char ch)
{
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}

MediaResult media_id_from_hex(const char *text, size_t bytes, uint8_t *output)
{
    if (text == NULL || output == NULL || bytes == 0 || bytes > 20 || strlen(text) != bytes * 2) { return MEDIA_INVALID; }
    uint8_t value[20];
    for (size_t i = 0; i < bytes; ++i) {
        int a = hex(text[i * 2]), b = hex(text[i * 2 + 1]);
        if (a < 0 || b < 0) { return MEDIA_INVALID; }
        value[i] = (uint8_t)(a * 16 + b);
    }
    memcpy(output, value, bytes); return MEDIA_OK;
}

MediaResult media_id_to_hex(const uint8_t *data, size_t bytes, char *output, size_t capacity)
{
    static const char digits[] = "0123456789abcdef";
    if (data == NULL || output == NULL || bytes == 0 || bytes > 20 || capacity <= bytes * 2) { return MEDIA_INVALID; }
    for (size_t i = 0; i < bytes; ++i) {
        output[i * 2] = digits[data[i] >> 4]; output[i * 2 + 1] = digits[data[i] & 15];
    }
    output[bytes * 2] = '\0'; return MEDIA_OK;
}
