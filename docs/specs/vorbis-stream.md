# Bounded Vorbis push decoding and seeking (#16)

## Evidence and scope, recorded before implementation

This module consumes the logical, decrypted Vorbis stream specified by #15.
It introduces no Spotify endpoint or reverse-engineered native behavior. No
XPUI or native binary inspection is needed. The implementation contract comes
from the supplied pinned public codec source and open Ogg/Vorbis standards.

`vendor/stb_vorbis.c` v1.22, stb commit
`2c980bb59875b0d32144a71867fbdebb2f77cd20`, SHA-256
`4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`:

* Lines 118-139: supplying `stb_vorbis_alloc` bounds setup and temporary memory.
* Lines 177-231: retain unconsumed compressed bytes; packets are not inherently
  bounded. Frame output is temporary planar float and must be copied before
  the next decode call. Opening needs all three headers.
* Lines 233-245, 4341-4545: push mode has flush/resync, not seek. After flush,
  CRC scanning consumes a page to establish its granule position, and the next
  audio packet establishes overlap before PCM becomes usable.
* Lines 3380-3440: the first packet is discarded; final granules trim output.
  Nonzero initial PCM offsets are not fully implemented upstream. Signed
  sample-offset access and internal 32-bit positions impose a local limit.

Consulted librespot `19959512c212cbc48a3ff64382fe204f52c659a7`,
`playback/src/player.rs:1150-1235`: decoder selection is separate from byte
decryption and seeking. This is source context, not a Spotify protocol claim.

Primary format references consulted on 2026-10-18:
[Ogg framing](https://xiph.org/ogg/doc/framing.html) defines lacing, little-endian
headers and CRC polynomial 0x04c11db7 with zero initial/final values;
[Vorbis encapsulation](https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-132000A.2)
places identification alone on the first page, separates setup from audio,
and defines granules as the end PCM position of the last completed packet.
The final granule trims the tail. A page without a completed packet uses -1.

## Memory, formats and ownership

Each decoder borrows one fixed 256 KiB codec arena and one fixed 64 KiB work
arena; current/next together fit 512 KiB codec plus 128 KiB work. No silent
growth or heap fallback. Work contains an unconsumed compressed window, a
bounded sparse page index, copied interleaved frame output, conversion state
and decoder metadata. The compressed page/setup/packet window is 28 KiB; larger
valid pages and setup packets return LIMIT. Large valid streams can exceed these deliberate limits
and return LIMIT or NO_MEMORY. Setup and packets must fit the compressed
window; compressed pages are also bounded by that window. Two-channel maximum,
4096-frame maximum decoded block, native rates 8000..192000, float32 output.
Injected `AudioFormat` fixes output rate/channels for an epoch. Mono duplicates
to stereo; stereo averages to mono. Unsupported formats/chains/multiplexing and
nonzero initial PCM offset conventions are outside the supported policy;
EOF/granule count mismatches are rejected rather than inventing trim values.
Source sizes are limited to UINT32_MAX logical bytes and page granules to
INT32_MAX native frames because of the pinned codec's position interface.

The decoder thread exclusively owns all APIs and its source callback. It may
run blocking fixture reads, or use a nonblocking range/decrypt adapter that
returns NEED_MORE and arranges an owner-thread request. Wrong-thread mutations
and source-callback reentry return BAD_STATE. No decoding runs in a
WASAPI callback or UI frame. The byte callback receives the generation and
absolute logical offset, and never supplies borrowed plaintext after returning.

## Streaming and seek contract

`decoder_open/push/read_frames/seek/close` retain partial headers/packets, copy
stb planar output before reuse, distinguish backpressure/need-more/EOF/errors,
and require matching generation IDs. Caller output contains interleaved float
frames and is tagged by the current generation. Explicit EOF must include a
valid final Ogg page and its exact granule; a partial page/packet is truncation.
All pages are CRC/serial/sequence/continuation checked before stb sees them.

Seeking executes on the decoder owner after a queued command handoff. A strictly
newer generation clears pending PCM and compressed input before acknowledgement;
old byte completions and frame reads return STALE. Sink integration later must
use this epoch to discard previously enqueued PCM. Calls on one decoder do not
run concurrently.

A fixed sparse index records CRC-validated page offsets/granules and compacts
when full. Seek refinement uses bounded logical `read_at` probes (including
the tail) and byte bisection within known granule bounds. Choose an anchor at
least two maximum blocks before the target, flush/resync, establish overlap,
then decode/discard to the exact target. At the beginning, reopen headers and
decode from zero. No whole-track buffer or nonexistent stb push-seek API.
Targets and reported positions use output-format frames; source granules use
native frames. Rational resampling remains anchored to the global output frame
grid across chunk boundaries and seeks. A bounded linear interpolator provides
rate conversion; this is an initial deterministic converter, not a high-order
antialiasing filter. EOF extends the last source sample only to complete the
defined `ceil(native_frames * output_rate / native_rate)` output count.

## Verification plan

Generate synthetic Ogg and independent float PCM goldens with pinned SoundFile
0.13.1/libsndfile and NumPy 2.2.6; record versions and SHA-256s. Compare PCM with
a documented float tolerance and exact frame counts. Exercise byte/random
fragmentation, native/injected rates, both channel policies, EOF/truncation,
bad capture/CRC/sequence/granules, oversized setup/packets, fixed-arena exhaustion,
index compaction/refinement, page-boundary and repeated seeks, and stale epochs.
Verify no application heap allocations during decode and both 256 KiB codec
arenas plus both 64 KiB work arenas remain fixed. Run the entire Debug ASan
CTest suite with strict compiler warnings before committing and closing #16.

## Verification results

All 29 default tests passed under MSVC Debug `/W4 /WX` and AddressSanitizer,
including `audio_vorbis_stream` and `audio_seek`. The pinned stb implementation
is unchanged; its include wrapper locally contains only upstream narrowing
and variable-shadow diagnostics C4244/C4245/C4456/C4457. All project code keeps
strict warnings. Pull/file APIs and the upstream heap fallback are disabled.

SoundFile 0.13.1 (libsndfile 1.2.2), NumPy 2.2.6 generated the synthetic corpus
and independent goldens. Exact frame counts and absolute float error <=3e-5
passed for mono 22050 Hz, stereo 44100 Hz, stereo 48000 Hz, mono-to-stereo at
48000 Hz and stereo-to-mono at 32000 Hz. The largest fixture has 432137 native
frames and 24 pages. Fixture identities and per-page positions are recorded in
[`tests/fixtures/vorbis/manifest.json`](../../tests/fixtures/vorbis/manifest.json).
`tools/vorbis_fixtures.py` verifies file hashes and independently re-decodes each
Ogg golden. `--write` explicitly regenerates synthetic media (container serials
can change); no account data or external recordings are used.

Measured codec setup+temporary peaks are 127680 bytes (mono22), 139289 bytes
(stereo44) and 123873 bytes (stereo48). Each workspace uses 63904 bytes. Tests
also keep two decoders live, assert fixed arena usage, verify clean exhaustion
with smaller codec/work buffers, check storage wiping and zero CRT heap calls,
and close the fake device thread handle. This corpus validates the initial
budget, not every possible valid Vorbis setup: exceeding a cap is a supported
explicit failure.

Seeking compares against both linear stb decode and independent goldens at
start/middle/end, every corpus audio-page boundary and deterministic random
positions. Cold tests require at least two range probes, a nonzero anchor and
actual flush/resync, with fragmented/pending reads and cancellation during
refinement or header reopening. Fifty superseded epochs reject old input/PCM.
CRC, capture, serial, sequence, invalid audio packet type, granule limits,
truncated/missing EOS, oversized page/setup admission, sparse-index compaction,
and fake device-thread callback rejection are covered. Production decoder
memory is fixed; only the test oracle stores complete short fixture files.
