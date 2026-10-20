# Detail routes: incremental verification (#43)

2026-10-20. Issue #43 is **not complete**. Its shared visual reference/components
(#41) and shell (#42) are still open. This change provides tested interaction
and artwork improvements plus evidence for the remaining implementation.

## Delivered behavior

Playlist primary Play chooses a playable occurrence without requiring a prior
selection; a visible playable selection takes precedence. The result is cached
when the bounded filter result is published. Empty, pending, failed and retired
sources have no target. Existing revision/ordinal identity, filtering, sorting,
10k windows and finite copied queues are retained.

Album/Artist headers use existing detail metadata and request real detail images
through the bounded artwork service during preparation. Copied title, subtitle
and artwork survive track-only continuation responses. Navigation retires their
generation and image references. No follower count is invented when absent.
Play first track is explicitly labeled for the existing single-track service;
it does not promise a complete album queue. List keyboard focus starts at the
first stable row. Detail layout, keyboard scrolling and visible artwork use the
same reduced viewport below the header.

Catalog and playlist title controls share a left-aligned row control with
hover, disabled, focus and compact-ID current-track states. It uses the existing
input capture machinery. Playlist rendering does not inspect worker-owned index
state while a filter replacement is pending. Unsupported, restricted, missing
and rate-limited catalog outcomes have distinct readable messages.

## Automated checks

- Clean snapshot: **54/54 Debug ASan tests passed**, including the deliberate
  arena-overrun checks. See [asan-clean.txt](detail-routes/asan-clean.txt).
  The snapshot starts at `7a1dcb9` and includes only the files in this change.
  Pinned yyjson bytes were restored from the unchanged Windows checkout after
  `git archive` normalized their line endings; hashes match the vendor manifest.
- Shared row tests exercise pointer capture, keyboard activation, disabled and
  current-track color states and 1000 allocation-free layouts. Catalog tests
  cover primary playback admission, retained header metadata on continuation,
  hero/list/player geometry at 96/120/144/192 DPI, artwork failure/retirement,
  stale deliveries, 600 navigation transitions and 1000 allocation-free frames.
- Playlist tests retain the 10k index and <=16 materialized rows, and add
  no-selection/unavailable/hidden-selection/filter-pending primary Play cases.
  They exercise the actual copied queue through keyboard activation. Existing
  2000 warm layouts and 1000 DirectWrite/Clay/render frames remain allocation-free.
- Clean Release compiles with `/W4 /WX`. The unchanged #35 source/dependency/
  imports/size audit passes: **370 C units, six artifacts, zero failures**.
  `spotic.exe` is **1,012,224 bytes**. See
  [release-audit.json](detail-routes/release-audit.json). This is not a fresh
  resident-memory/startup/physical-device performance acceptance.
- The existing modified workspace has additional opt-in tests: **58/59 passed**;
  `ui_caption_visual` fails at its unchanged `SetForegroundWindow(other)`
  assertion. See [asan-workspace.txt](detail-routes/asan-workspace.txt).
  The initial idle-window failure disappeared after releasing Computer Use's
  helper. One clean Dealer loopback failure passed its focused rerun and the
  subsequent full 54-test run. No test was disabled or weakened for this change.

The app remains below its 256 KiB state cap (245160 bytes). Playlist/index/shared
collection model state is 511424 bytes, below the existing 512 KiB reservation.
Production modules changed here remain below 500 lines. No endpoint, JSON
contract, background timer or render-loop allocation is added.

## Visual evidence and reproduction

DX11 WARP captures cover all five routes at **400/1100/1440 DIPs wide**, 720 DIPs
high, and **96/120/144/192 DPI**: 60 PNGs under
[detail-routes](detail-routes/). They are synthetic route fixtures with deliberate
artwork placeholders; they do not demonstrate a successful live image fetch.
Representative captures:

| Route | Default / 100% | Narrow / 200% |
| --- | --- | --- |
| Playlist | [default](detail-routes/playlist-default-96.png) | [narrow](detail-routes/playlist-narrow-192.png) |
| Search | [default](detail-routes/catalog-search-default-96.png) | [narrow](detail-routes/catalog-search-narrow-192.png) |
| Album | [default](detail-routes/catalog-album-default-96.png) | [narrow](detail-routes/catalog-album-narrow-192.png) |
| Artist | [default](detail-routes/catalog-artist-default-96.png) | [narrow](detail-routes/catalog-artist-narrow-192.png) |
| Lyrics | [default](detail-routes/catalog-lyrics-default-96.png) | [narrow](detail-routes/catalog-lyrics-narrow-192.png) |

After the Debug build, run the two test binaries with `--visual` from the build
directory, then `python tools/route_captures.py build docs/verification/detail-routes`
from the repository root. WARP captures require the existing optional GPU
capability; the normal unit suite does not require enabling those CTest entries.

The user explicitly approved reference captures and read-only live route checks.
Computer Use observed the installed **Spotify 1.3.5.3058** at a 1600x900 window,
including Playlist, Search, Artist, Album and an unsynchronized Lyrics state.
Reference DPI was not independently measured. The extracted manifest is the
older **1.3.4.258**; current runtime and historical static evidence are separate.
Public main-pane crops for [Search](detail-routes/reference-search-main.png) and
[Album](detail-routes/reference-album-main.png) exclude the account/library and
player panels. These [Search](detail-routes/comparison-search.png) and
[Album](detail-routes/comparison-album.png) side-by-side images label different
content and viewport sizes. They expose structural gaps, not a pixel-parity pass.
The runtime Artist has a full-width banner and Popular section; Lyrics uses much
larger text on a colored panel. Those remain visibly different in the fixtures.

## Live compatibility

An explicit read-only workspace diagnostic reused the existing saved-session,
TokenManager, catalog requests and transport. It made no playback command and
printed no credentials, account identifiers, URLs or response bodies. Numeric
results are in [live-results.txt](detail-routes/live-results.txt).

| Route | Actual result |
| --- | --- |
| Search | Internal authorization accepted; request HTTP 403; no page. |
| Album | HTTP 403; no page. |
| Artist | HTTP 403; no page. |
| Lyrics | Not requested: no accepted live track/artwork pair from those routes. |
| Playlist | Not re-probed here; preceding workspace integration accepted its live disk-index/metadata flow. |

The diagnostic used the pre-existing, uncommitted live adapter; it is independent
of the clean committed-source fixture acceptance. The 403 responses require
further REA evidence before any header/client identity/contract changes. No
alternative endpoint or fixture result was substituted for live success.

## Remaining issue acceptance

| Area | Remaining work |
| --- | --- |
| Shared reference | #41 measured tokens/primitives and keyboard/state matrix; #42 shell layout. |
| Playlist | Cover/metadata hero and image adapter, reference column composition, complete sticky scrolling and menu interactions. |
| Search | Reference top result/cards/shelves, category styling, full empty/loading/error capture matrix. |
| Album/Artist | Reference-scale hero/banner, disc/popular/release composition, additional available navigation/actions, reference menus/tooltips. |
| Lyrics | Reference typography/panel/scroll controls and full live consumed-timeline acceptance. Existing timeline/follow tests pass. |
| Visual/live | Matching-content reference comparisons for every route/state at all widths/scales; keyboard-only native acceptance; resolve live 403s and the opt-in caption foreground test. |

The issue must remain open until these acceptance criteria are satisfied.
