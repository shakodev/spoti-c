# Encrypted headless fixture

Original seeded stereo noise, encoded with libsndfile/libvorbis at nominal
320,000 bit/s (quality 9). The 12.002854-second file averages 362,806 bit/s
including container overhead. It is VBR, not a claim of constant-rate encoding.
The manifest records exact frame counts, tool versions, sizes and SHA-256s.

`synthetic.enc` is AES-128-CTR of a public synthetic 167-byte prefix plus
`synthetic.ogg`. Key: bytes 00 through 0f. IV: the public protocol constant
documented in the existing CTR spec. `synthetic.f32` is an independent native
48-kHz stereo float32 libvorbis oracle. No account data or Spotify recordings.

Reproduce deliberately with the pinned test environments:
`python tools/headless_fixtures.py --write`. Without `--write`, hashes are
verified. Encoder writes use bounded chunks to avoid a libsndfile Windows stack
overflow with large single writes. Production code does not link Python or
the oracle libraries. Serve only the ciphertext file through fixture mode.
