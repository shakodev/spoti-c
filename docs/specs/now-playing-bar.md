# Spotify desktop player bar (#44)

Evidence and implementation contract, recorded before implementation on 2026-10-20.

REA MCP parsed `dwp-now-playing-bar.js` and `xpui-pip-mini-player.js` in the
extracted XPUI tree, SHA-256
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Parent evidence:
`ev_8cfe785e95bba5e1785f4b307754d7e9c7c87d3e71f204aad9e84d3fd55360ee`.
Two scripts / 96832 bytes parsed without failures. Other 269 sources and dynamic
relationships remain unknown; bounded semantic coverage is partial. Static
analysis did not execute Spotify or establish exact runtime interaction timing.
The extracted manifest pins 1.3.4.258; the separately observed installed desktop
reference is 1.3.5.3058. Neither is asserted to be the latest version.

The observed desktop reference groups artwork/title/artist at the left, transport
over elapsed/seek/duration in the center, and secondary controls/mute/volume at
the right. Play/pause is a white circular primary action; secondary icons use
quiet backgrounds and visible hover/focus/disabled states. The previous native
two-row text-button layout does not match that structure.

Implementation details, capability boundaries and measured checks follow below.

## Implementation contract

- Use a bounded three-part bar: 56-DIP cover and 14/12-DIP metadata at the left,
  a centered 32-DIP primary transport above 4-DIP seek rail and separate 12-DIP
  elapsed/duration labels, and 16-DIP secondary icons with 32-DIP pointer targets.
  Base height is 88 DIPs. Compact windows retain transport, seek and volume in
  an explicit bounded 120-DIP composition; they do not squeeze overlapping rows.
  The narrow adaptation is a native policy until a matching desktop reference
  at that width exists. Main content keeps its existing player-bar boundary.
- Icons are original procedural vector shapes rendered by the existing DX11
  instanced pixel shader; no new font, image dependency or per-frame allocation.
  Keyboard focus and pointer hover show text labels, including unavailable
  control reasons. Reduced motion requires no animation because states redraw
  only on existing input/state events.
- The existing Player owner supports play/pause, previous/next, seek, app volume,
  shuffle and repeat. Publish repeat/shuffle and finite queue position in the
  immutable snapshot instead of predicting successful commands in the UI.
  Lyrics uses the existing route. Queue inspection requires an existing queue
  snapshot binding. Device selection, miniplayer and fullscreen are unavailable
  in this client and must say why when focused/activated.
- Track metadata is keyed by compact track ID, independent of navigation
  generations. Never display one track's title/art under another track ID.
  Preserve bounded copies for known catalog/playlist items. The existing media
  resolver may project Track.name, first Artist.name and Album cover identifiers
  from the already pinned proto/metadata.proto definitions. This adds no endpoint,
  header, persisted hash or guessed JSON schema. Record the actual artwork URL
  authority before introducing any identifier-to-URL formatting.
- Seek labels and fill use consumed frames. During buffering/starvation keep the
  last authoritative consumed position; no extrapolation or cosmetic progress.
  Drag preview never submits a seek until release. Cancel, track replacement,
  invalidation or an external seek cannot commit a stale gesture. Volume retains
  #38's acknowledgement tickets, remembered nonzero level and failure behavior.
- Metadata publication is a bounded owner-to-UI copy, outside layout and audio
  callbacks. Cover preparation uses the existing artwork cache; navigation may
  retire route artwork but must re-pin the current track cover. No route page is
  retained merely to display the player bar.

## Existing cover URL reference

The pinned ordinary source references independently define image ID formatting:
`ref/librespot/metadata/src/audio/item.rs:74` uses
`https://i.scdn.co/image/{file_id}`, and
`ref/cspot/cspot/src/TrackQueue.cpp:96` concatenates the same base with the
hex-encoded image ID. These are source observations, not executed native code.
The repository manifest pins librespot commit
`19959512c212cbc48a3ff64382fe204f52c659a7`. The UI's existing artwork fetcher
already handles unauthenticated HTTPS images. Use exactly a validated 20-byte
cover ID encoded as 40 lowercase hex characters; no auth headers or redirects.
This does not change a Spotify API request. Missing cover IDs remain explicit.

## Capability matrix

| Control | Binding / behavior | Unavailable state |
| --- | --- | --- |
| Play/pause, previous/next | Existing Player submission; snapshots acknowledge state | Sign-in, service, stopped/error reasons |
| Seek | Consumed frames; release-only commit; arrows five seconds, Home/End | Unknown duration or invalid timeline (except paused) |
| Volume/mute | Existing ticket receipts, continuous drag, remembered nonzero level | Sign-in or playback binding missing |
| Shuffle | Existing bounded Player queue shuffle | Same admission as transport |
| Repeat | Existing Player repeat enum; explicit labels cycle off â†’ one â†’ all â†’ off | Same admission as transport |
| Lyrics | Existing lyrics route and catalog service; no new endpoint | Explains catalog/track dependency |
| Queue | Read-only finite owner queue, current marker and keyboard-accessible earlier/later pages | Explains missing queue service; uncached names say metadata pending |
| Devices | No device selection service bound to this shell | Focus/activation explains Connect dependency |
| Mini player/fullscreen | No supporting window service | Focus/activation explains missing window service; omitted at smaller widths |

The repeat order and deterministic shuffle seed are native client policies, not
claims about Spotify's runtime implementation. A further REA literal trace for
`repeat` returned no matches in the partial selected graph
(`ev_a6fa9c00124aca48d2dc71cf9a6653d66ca0f0489afd8cc923c32c09276ce139`);
this does not establish absence. No Spotify repeat-cycle runtime was changed to
infer its order. Unavailable buttons are dimmed but remain focusable for their
reason. Full tooltip text is retained by the existing Read messages transcript.

## Verification

On 2026-10-20 the complete Debug/ASan command
`ctest --test-dir build --output-on-failure` passed **55/55** tests in
**64.19 seconds**. Strict-warning Debug and Release builds passed. Checks cover
bounded UTF-8 metadata/cover projection, handoff and eviction, icon admissions,
centered/narrow/DPI geometry, queue paging, actual Player commands, track changes,
frozen buffering clocks, external generations and cancelled/stale seeks. Existing
allocation, gapless, scheduler, SMTC and volume-receipt tests pass. New regressions
cover background owner roles, client-token lifetime bounds, provider status 200,
metadata duration superseded by exact decoder length, strict/opted-in Ogg tails
against independent PCM, and 24 source-retirement cycles in one runtime.

Release regression tests and the unchanged dependency audit passed: **387 C
translation units, six artifacts, zero failures**, `spotic.exe` **1,051,648
bytes**. Native HWND/WinRT metadata/status/timeline and teardown also passed.
The source/import/size audit does not certify the complete #35 performance gate.
No release threshold or idle timer was changed. App state remains within its
256 KiB cap, new C modules stay below 500 lines, and warm-frame/callback allocation
checks still pass.

`capture_player_bar` records nine explicit WARP/fixture captures through actual
bounded metadata/artwork services and asserts the displayed cover pixel. These
cover normal/narrow/scaled, mixed long RTL, signed-out/stopped/buffering/playing/
paused/error states. The pixel assertion caught a Clay painter-order bug where
an opaque custom-image background covered the texture.

![Same-scale desktop reference and native fixture bar](images/issue44/normal-comparison.png)

![Side-by-side reference and native fixture bar](images/issue44/normal-side-by-side.png)

![Narrow native fixture layout](images/issue44/player-bar-narrow-playing-400x720.png)

![Long mixed RTL title](images/issue44/player-bar-rtl-long-1100x720-strip.png)

The read-only reference is Spotify 1.3.5.3058. A notification occludes its
rightmost area; no hidden pixels were reconstructed. Accessibility supplied
additional observed control labels. The compact composition is a local policy,
not an invented claim about an unrecorded narrow Spotify layout.

The user subsequently approved account/WASAPI playback and desktop/GPU checks.
The Release live check passed commands, frozen pause, acknowledged seek and
volume/mute, next/previous metadata, modes, queue, route persistence and balanced
shutdown. Actual cover downloads rendered on hardware DX11 at normal and narrow
widths and changed with the next track. The visible offline-demo native check
independently passed pointer drag, keyboard, Settings, compact controls and
process restart persistence. Detailed observations, limited REA runtime
projections, screenshots, exact scope and repeatable opt-in instructions are in
[now-playing-live-acceptance.md](now-playing-live-acceptance.md); artifact/capture
hashes and final results are in `now-playing-live-validation.json`.

Prerequisites #41's full visual foundation and #42's account-backed shell routes
remain independent open work. The committed desktop host still binds explicit
offline demonstration services; the live harness binds the same player-bar App
module to the account/WASAPI runtime. Live offscreen artwork and visible offline
desktop validation are separate checks. Global SMTC flyout/listener and physical
media-key observation remain manual. Nothing here closes or waives #35, #39,
#41 or #42.
