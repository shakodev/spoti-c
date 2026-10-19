# Bounded lyrics and audible timeline (#24)

Recorded before implementation. This extends [lyrics.md](lyrics.md),
`pathfinder.md`, `player.md` and `audio-sink.md`. No new endpoint or hash is added.

REA inspected the issue's two XPUI files under parent
`ev_ce0ccb549a86a39853a36319ef3c5ef95c05974b59f62db64c4095ca70a85ac1`,
tree SHA-256 `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Both parse without failures; semantic coverage is bounded/partial and269 other
files remain unselected. The predecessor's exact snapshot modules2200/24063
were read by REA parent
`ev_b8f841d1f93d4a5d9ad0cb54409ce41a1fcd5791c9df07ab8746771b284d5776`.
They parse with no node truncation; relationships outside these two files remain
unknown. Historical positive/404 records1/2 of `api21-errors.har` were inspected
under parent `ev_4a6129675e34fdb8d094b401eabe4d7f5eaf0cdea9417ba8de4690b9b1e105aa`,
capture SHA-256 `bbb3c9037b2794c7248a7f58c390bfc8ba1f64477aeea35f4b69189aebda7d2c`.
The verified seven-record capture was also read under parent
`ev_f1bc65554c5932a3c3330e8448971219b6b90db74162d91cd0c5c290ef5d6e27`.
Full returned summaries and limitations: `lyrics-timeline-rea.json`.
An earlier similarly named capture was empty and supplies no response evidence.
These are inert source/historical captures, not live-account compatibility.

## Request and transport

Use the canonical API_LYRICS contract, not PathFinder. Module2200 builds GET
`https://spclient.wg.spotify.com/color-lyrics/v2/track/{trackId}/image/{image}`.
Track ID is22-character base62 from the Spotify track URI. Both arguments use
encodeURIComponent semantics; the artwork is one encoded path component.
Query is format=json, vocalRemoval=true/false, optionally clientLanguage and
session market. Module24063 chooses system language/all on feature branches;
the caller supplies that value explicitly. Empty artwork does not issue a request.

Headers come from the contract allowlist: Bearer Authorization, Accept JSON,
App-Platform, truthful session Spotify-App-Version and optional Accept-Language.
There is no Content-Type, client-token, operationName or persisted hash. The
WinHTTP adapter validates the exact service origin through TokenManager, then
copies only its bearer header. No redirects or automatic retries are requested.
401/429 and other failures remain explicit;404 publishes an unavailable document
without attempting JSON parsing. Token-manager status reporting does not replay
the request. An explicit IPv4 loopback fixture origin permits fake authorization
only in deterministic tests and never relaxes the production HTTPS origin.

The request API also supports externally driven transport when HttpClient is
absent, retaining the same ticket/buffer lifetime. Late track generations cannot
publish. Cancelled buffers stay reserved until transport's terminal acknowledgement.

## Compact document

Parse strictly in situ using the shared two-slot JsonPagePool from #23. Bound
combined lines plus preview rows to2048, and compact document plus retained input
to256 KiB. Typed spans borrow that input. Build compact metadata in bounded owner
scratch, free the transient DOM, then materialize the document in the same page's
DOM storage. JsonPage control/reference counters remain outside the payload so
final release can poison input/document safely under ASan. There is no extra
private body pool, text copy or heap fallback. Normal API pool limits also apply.

Fields include raw syncType, language/provider/display name/provider ID, raw
capStatus, optional vocal-removal/RTL flags and bounded line spans. Module24063
marks LINE_SYNCED and SYLLABLE_SYNCED as timed but reads only line startTimeMs
and words. The positive fixture observes LINE_SYNCED and capStatus NONE; no
nonempty syllable schema or word-level timing is established. SYLLABLE_SYNCED
therefore supports its evidenced line boundaries only. Other raw modes are
retained as plain text with no timing, without inventing a recognized enum.
Tests for these source branches are labeled synthetic, not service observations.

Timestamps are decimal uint64 strings, checked for overflow and nondecreasing
order. UINT64_MAX is reserved for no deadline and is rejected as a line start.
Duplicate starts remain in order; the last equal-time row wins. Lines end at
the next strictly later start, and the final end is unbounded. endTimeMs=0 in
the observed reply is not treated as an actual line end. Plain rows may omit
timestamps; supplied timestamps still undergo decimal/order validation.
Empty/multilingual text is valid. capStatus other than NONE exposes only preview
rows through a capped availability state, with no automatic highlighting timer;
unknown rights values never promote a preview to full lyrics.

## Playback and scheduling

The timeline retains its document and consumes a freshly refreshed PlayerSnapshot.
position_frames already comes from sink_info.played_media_frames through player
markers: hardware-clock/fallback latency is handled there, and silent underruns
do not advance it. Do not subtract latency twice, use submitted frames, or derive
position from a UI wall-clock timer. The timeline is independent of player epoch
changes on same-track seek/pause/device reopen; track identity must still match.

On input/state/seek/device events and a lyric deadline, the decode owner refreshes
the player and supplies a new snapshot. The UI receives the immutable result.
An owner may combine the next lyric timeout with player_next_wait_ms and its
existing event wait. This adds no thread, Win32 timer or60 Hz polling loop.
Hidden/plain/missing/capped lyrics, paused/stopped/loading/seeking/buffering/error
states, invalid clocks, track mismatch and the final line produce no deadline.
Paused/ended valid positions can still display a line; buffering displays the
last confirmed audible position. A deadline never changes the line by itself.
The owner re-queries audible audio at wake-up, so latency changes/drift can delay
but cannot advance a line ahead of the device.

Binary search selects the last start <= audible position. Fractional media frames
and milliseconds use checked division and ceiling for the next boundary, avoiding
early wakes or integer overflow. Seek recalculates directly; no monotonic UI
clamp prevents a backward seek. Fake-clock and real player/fake-backend tests
cover deadlines, latency changes, drift, pause, seek, device-rate changes and
underrun silence. All page, transport and fake-player handles must return to baseline.

## Verification and measurements

The x64 compact document occupies 49,304 bytes inside its existing page slot.
Together with the maximum padded 140 KiB input, it uses at most 192,668 bytes,
below the 256 KiB document/text ceiling. Lyrics control/HTTP state is 17,408
bytes; collections, catalog and lyrics controls total 124,520 bytes, below the
shared 512 KiB model budget. The single shared API pool remains 1,040,416 bytes.
The transient DOM is freed before compact publication, with no retained DOM
nodes or external lyric text allocation.

`api_lyrics` verifies the unchanged 104-line positive and status-only 404
predecessor fixtures, synthetic syllable/plain/unknown/capped branches, Unicode,
empty text, malformed/descending/overflow timestamps, last-duplicate behavior,
2048/2049-row admission, body/DOM exhaustion and stale/cancelled generations.
Two UI reader threads retain/release concurrently; ASan verifies both text and
compact payload become poisoned only after final release. One thousand repeated
navigation cycles restore shared reservations to zero.

The deterministic WinHTTP fixture captures the exact encoded GET and header
allowlist, tests 404 non-JSON/401/429/redirect rejection, stale active replacement
and terminal cancellation. Its explicit expected-disconnect option allows a
cancelled peer to close before response headers without stopping the fixture
from serving the replacement request. This flag is disabled in other cases.
Metrics account for all HTTP/server handles; no retry or redirect is emitted.

Real Player/Sink code with the existing fake backend verifies latency changes,
clock drift/fallback, fractional-frame boundaries, same-track pause/backward
seek, buffering silence, device-rate changes and track mismatch. Fake monotonic
time and allocation auditing check hidden/paused/plain/capped deadline behavior.
These are local deterministic checks, not live-account or physical-device proof.

Final required checks passed: Debug configure with ENABLE_ASAN=ON, the MSVC C17
build with `/W4 /WX /fsanitize=address`, and
`ctest --test-dir build --output-on-failure`: 40/40 tests passed with no compiler
warnings or sanitizer errors. Live account/device tests remained opt-in and off.
