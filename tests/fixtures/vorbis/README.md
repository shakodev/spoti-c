# Synthetic Vorbis corpus

Generated locally by `tools/vorbis_fixtures.py` using the pinned test-only
requirements in `tools/vorbis-requirements.txt`. Signals are original chirps,
tones and seeded noise; there are no external recordings or Spotify media.

Ogg encoding and native PCM goldens use SoundFile/libsndfile (libvorbis),
independent of stb. Rate/channel goldens use NumPy interpolation. The manifest
records oracle versions, SHA-256s, exact native/output frame counts and Ogg page
positions. Native PCM is little-endian interleaved binary32 without a header.

Run the fixture tool without arguments to verify hashes and independent native
decode. Use `--write` only to regenerate deliberately; Ogg serial numbers may
change. C tests compare exact frame counts and float error <=0.00003. These
tools/libraries are never linked into the production module.
