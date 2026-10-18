#include "audio/spotify_header.h"
#include "support/test_check.h"
#include <math.h>
#include <string.h>

static void put(uint8_t *p, uint32_t bits)
{
    for (unsigned i = 0; i < 4; ++i) { p[i] = (uint8_t)(bits >> (8 * i)); }
}
static void probe(uint8_t p[SPOTIFY_OGG_PROBE])
{
    memset(p, 0xa5, SPOTIFY_OGG_PROBE);
    put(p + 144, 0xc0c00000); put(p + 148, 0x3f400000);
    put(p + 152, 0xc0400000); put(p + 156, 0x3fa00000);
    memcpy(p + 167, "OggS\0", 5);
}
int main(void)
{
    uint8_t storage[SPOTIFY_OGG_PROBE + 1], *p = storage + 1; /* unaligned LE floats */
    probe(p); SpotifyHeader h, before = {1,2,3,4};
    for (int32_t format = 0; format <= 2; ++format) {
        CHECK(spotify_header_parse(format, p, SPOTIFY_OGG_PROBE, &h) == SPOTIFY_HEADER_OK);
        CHECK(h.track_gain_db == -6 && h.track_peak == 0.75f && h.album_gain_db == -3 && h.album_peak == 1.25f);
    }
    float gain;
    CHECK(spotify_header_gain(&h, SPOTIFY_GAIN_OFF, &gain) == SPOTIFY_HEADER_OK && gain == 1);
    CHECK(spotify_header_gain(&h, SPOTIFY_GAIN_TRACK, &gain) == SPOTIFY_HEADER_OK && fabsf(gain - 0.5011872f) < 0.000001f);
    CHECK(spotify_header_gain(&h, SPOTIFY_GAIN_ALBUM, &gain) == SPOTIFY_HEADER_OK && fabsf(gain - 0.7079458f) < 0.000001f);
    h = (SpotifyHeader){6, 2, 60, 0};
    CHECK(spotify_header_gain(&h, SPOTIFY_GAIN_TRACK, &gain) == SPOTIFY_HEADER_OK && gain == 0.5f);
    CHECK(spotify_header_gain(&h, SPOTIFY_GAIN_ALBUM, &gain) == SPOTIFY_HEADER_OK && gain == 1);
    h = (SpotifyHeader){-60,64,60,0};
    CHECK(spotify_header_gain(&h, SPOTIFY_GAIN_TRACK, &gain) == SPOTIFY_HEADER_OK && fabsf(gain - 0.001f) < 0.000001f);
    for (size_t bytes = 0; bytes < SPOTIFY_OGG_PROBE; ++bytes) {
        h = before; CHECK(spotify_header_parse(0, p, bytes, &h) == SPOTIFY_HEADER_TRUNCATED);
        CHECK(memcmp(&h, &before, sizeof(h)) == 0);
    }
    CHECK(spotify_header_parse(0, NULL, 0, &h) == SPOTIFY_HEADER_TRUNCATED);
    CHECK(spotify_header_parse(0, NULL, 172, &h) == SPOTIFY_HEADER_INVALID);
    CHECK(spotify_header_parse(0, p, 172, NULL) == SPOTIFY_HEADER_INVALID);
    CHECK(spotify_header_parse(-1, p, 172, &h) == SPOTIFY_HEADER_UNSUPPORTED);
    CHECK(spotify_header_parse(3, p, 172, &h) == SPOTIFY_HEADER_UNSUPPORTED);
    const uint32_t invalid[] = {0x7fc00000,0x7f800000,0xff800000};
    for (size_t field = 0; field < 4; ++field) {
        for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
            probe(p); put(p + 144 + field * 4, invalid[i]); h = before;
            CHECK(spotify_header_parse(1, p, 172, &h) == SPOTIFY_HEADER_CORRUPT);
            CHECK(memcmp(&h, &before, sizeof(h)) == 0);
        }
        probe(p); put(p + 144 + field * 4, field % 2 == 0 ? 0x42740000 : 0x42820000); /* 61 / 65 */
        CHECK(spotify_header_parse(1, p, 172, &h) == SPOTIFY_HEADER_CORRUPT);
        probe(p); put(p + 144 + field * 4, field % 2 == 0 ? 0xc2740000 : 0xbf800000);
        CHECK(spotify_header_parse(1, p, 172, &h) == SPOTIFY_HEADER_CORRUPT);
    }
    probe(p); p[167] = 'X'; CHECK(spotify_header_parse(0, p, 172, &h) == SPOTIFY_HEADER_CORRUPT);
    probe(p); p[171] = 1; CHECK(spotify_header_parse(0, p, 172, &h) == SPOTIFY_HEADER_CORRUPT);
    h = (SpotifyHeader){NAN,1,0,1}; gain = 99;
    CHECK(spotify_header_gain(&h, SPOTIFY_GAIN_OFF, &gain) == SPOTIFY_HEADER_CORRUPT && gain == 99);
    CHECK(spotify_header_gain(&before, (SpotifyGainPolicy)3, &gain) == SPOTIFY_HEADER_INVALID);
    CHECK(spotify_header_gain(NULL, SPOTIFY_GAIN_OFF, &gain) == SPOTIFY_HEADER_INVALID);
    uint64_t offset = 99;
    CHECK(spotify_header_map(0, 1, &offset) == SPOTIFY_HEADER_OK && offset == 167);
    CHECK(spotify_header_map(UINT64_MAX - 167, 0, &offset) == SPOTIFY_HEADER_OK && offset == UINT64_MAX);
    CHECK(spotify_header_map(UINT64_MAX - 167, 1, &offset) == SPOTIFY_HEADER_LIMIT);
    CHECK(spotify_header_map(UINT64_MAX - 166, 0, &offset) == SPOTIFY_HEADER_LIMIT);
    CHECK(spotify_header_map(0, SIZE_MAX, &offset) == SPOTIFY_HEADER_LIMIT);
    CHECK(spotify_header_map(0, 0, NULL) == SPOTIFY_HEADER_INVALID);
    puts("audio_spotify_header passed"); return 0;
}
