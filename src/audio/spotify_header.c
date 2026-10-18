#include "spotify_header.h"
#include <float.h>
#include <math.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
    "IEEE binary32 normalization fields");

static float little_float(const uint8_t *p)
{
    uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float value; memcpy(&value, &bits, sizeof(value)); return value;
}
static bool valid(const SpotifyHeader *h)
{
    return isfinite(h->track_gain_db) && h->track_gain_db >= -60 && h->track_gain_db <= 60 &&
        isfinite(h->album_gain_db) && h->album_gain_db >= -60 && h->album_gain_db <= 60 &&
        isfinite(h->track_peak) && h->track_peak >= 0 && h->track_peak <= 64 &&
        isfinite(h->album_peak) && h->album_peak >= 0 && h->album_peak <= 64;
}
SpotifyHeaderResult spotify_header_parse(int32_t format, const void *data, size_t bytes, SpotifyHeader *out)
{
    if (out == NULL || (data == NULL && bytes != 0)) { return SPOTIFY_HEADER_INVALID; }
    if (format < 0 || format > 2) { return SPOTIFY_HEADER_UNSUPPORTED; }
    if (bytes < SPOTIFY_OGG_PROBE) { return SPOTIFY_HEADER_TRUNCATED; }
    const uint8_t *p = data;
    if (memcmp(p + SPOTIFY_OGG_PREFIX, "OggS", 4) != 0 || p[SPOTIFY_OGG_PREFIX + 4] != 0) { return SPOTIFY_HEADER_CORRUPT; }
    p += SPOTIFY_NORMALIZATION;
    SpotifyHeader h = {little_float(p), little_float(p + 4), little_float(p + 8), little_float(p + 12)};
    if (!valid(&h)) { return SPOTIFY_HEADER_CORRUPT; }
    *out = h; return SPOTIFY_HEADER_OK;
}
SpotifyHeaderResult spotify_header_map(uint64_t logical, size_t bytes, uint64_t *encrypted)
{
    if (encrypted == NULL) { return SPOTIFY_HEADER_INVALID; }
    if (logical > UINT64_MAX - SPOTIFY_OGG_PREFIX) { return SPOTIFY_HEADER_LIMIT; }
    uint64_t offset = logical + SPOTIFY_OGG_PREFIX;
    if (bytes > UINT64_MAX - offset) { return SPOTIFY_HEADER_LIMIT; }
    *encrypted = offset; return SPOTIFY_HEADER_OK;
}
SpotifyHeaderResult spotify_header_gain(const SpotifyHeader *h, SpotifyGainPolicy policy, float *out)
{
    if (h == NULL || out == NULL || policy < SPOTIFY_GAIN_OFF || policy > SPOTIFY_GAIN_ALBUM) { return SPOTIFY_HEADER_INVALID; }
    if (!valid(h)) { return SPOTIFY_HEADER_CORRUPT; }
    float factor = 1;
    if (policy != SPOTIFY_GAIN_OFF) {
        float db = policy == SPOTIFY_GAIN_TRACK ? h->track_gain_db : h->album_gain_db;
        float peak = policy == SPOTIFY_GAIN_TRACK ? h->track_peak : h->album_peak;
        factor = powf(10, db / 20);
        if (factor > 1) { factor = 1; }
        if (peak > 0 && factor > 1 / peak) { factor = 1 / peak; }
    }
    *out = factor; return SPOTIFY_HEADER_OK;
}
