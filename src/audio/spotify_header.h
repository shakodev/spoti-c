#ifndef SPOTIC_SPOTIFY_HEADER_H
#define SPOTIC_SPOTIFY_HEADER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SPOTIFY_OGG_PREFIX = 167, SPOTIFY_OGG_PROBE = 172, SPOTIFY_NORMALIZATION = 144 };
typedef enum SpotifyHeaderResult { SPOTIFY_HEADER_OK, SPOTIFY_HEADER_INVALID,
    SPOTIFY_HEADER_UNSUPPORTED, SPOTIFY_HEADER_TRUNCATED, SPOTIFY_HEADER_CORRUPT,
    SPOTIFY_HEADER_LIMIT } SpotifyHeaderResult;
typedef enum SpotifyGainPolicy { SPOTIFY_GAIN_OFF, SPOTIFY_GAIN_TRACK, SPOTIFY_GAIN_ALBUM } SpotifyGainPolicy;
typedef struct SpotifyHeader { float track_gain_db, track_peak, album_gain_db, album_peak; } SpotifyHeader;

/* Decrypted probe, including OggS/version at 167. Other prefix bytes are opaque.
 * Finite gains [-60,60] dB and peaks [0,64] are a local acceptance policy. */
SpotifyHeaderResult spotify_header_parse(int32_t format, const void *, size_t, SpotifyHeader *);
SpotifyHeaderResult spotify_header_map(uint64_t logical, size_t bytes, uint64_t *encrypted);
/* Explicit selection, no pregain: min(10^(gain/20), 1, 1/peak).
 * Zero peak means unknown; unity cap still applies. Output unchanged on error. */
SpotifyHeaderResult spotify_header_gain(const SpotifyHeader *, SpotifyGainPolicy, float *);
#endif
