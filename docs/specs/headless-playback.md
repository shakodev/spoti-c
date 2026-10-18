# Headless playback acceptance (#19)

Evidence recorded before implementation, 2026-10-18.

This integration reuses the existing media resolver, range/cache, CTR/header,
Vorbis, player and sink contracts. It introduces no Spotify endpoint, request
header, query hash or response format. The native-backed contracts remain in
`playback-resolution.md`, `transport-and-playback.md` and the existing audio specs.

REA MCP `analyze_javascript_application` inspected the extracted XPUI artifact,
selecting `xpui-snapshot.js`. Parent evidence:
`ev_0a25257b214ccb497b6c64977067e14f70fe045a3a98052b48a5d82ace794b1a`;
artifact SHA-256:
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
The selected file is snapshot infrastructure, not a streaming implementation.
Its static graph is partial and provides no new playback/API contract. The
receipt is retained in `headless-rea.json`; no live compatibility is inferred.

Source references consulted: `ref/librespot/playback/src/player.rs:45,588,1094,
1605,1984` (preload, seek, encrypted source and gapless policy),
`ref/librespot/audio/src/fetch/mod.rs:95,112,627,681` (prefetch/seek), and
`ref/cspot/targets/cli`. These are architecture references, not code to copy.

## Integration ownership

The network owner drives HTTP, cache, resolver, range requests and lease
retirement. The decode/device owner drives the player and sink. Each of the
two source slots carries one bounded read request and one prepared decryption
operation. Publication uses release/acquire atomics and events. Retire is
acknowledged only after the decoder relinquishes the source, outstanding
operations finish, ranges cancel and the media key retires. Shutdown joins
both owners before wiping storage. No whole-track plaintext buffer is used.

Device imports are deferred in MSVC builds. On opt-in WASAPI startup, USER32
initialization runs on the caller before creating the decode thread. A native
COM probe confirmed that first-time `CoInitializeEx` loads USER32 and can mark
its caller as a GUI thread; the audio owner's GUI-thread guard remains active.
The deterministic regression rejects a bad device-mode origin before opening
hardware, then verifies a fresh fake-output decode owner can still play.

Fixture mode serves original synthetic encrypted Vorbis through IPv4 loopback
HTTP, the actual encrypted disk cache and the same decryption/read handoff as
live playback. Fixture keys are public synthetic test data. Account credentials
are supplied separately through a private DPAPI credential record.

## Acceptance policy

Correctness runs in Debug with ASan, fake device and deterministic frame clock.
Compare decoded duration/PCM against the independent libvorbis corpus and
repeat stop, cancellation, source errors and shutdown. Account/device tests
are explicit opt-ins; unavailable live acceptance must be reported as skipped.

The Release benchmark runs for 600 real seconds with 320-kbps synthetic audio,
periodic seeks and gapless transitions. Save raw JSON samples and OS/CPU/device
identity. Measure complete process working set, private working set and private
commit with Windows APIs, including runtime, threads, HTTP, crypto and device
overhead. No working-set trimming. Fail when peak working set >=8,000,000 bytes.
Arena caps and ASan residency are never evidence for that resident target.
