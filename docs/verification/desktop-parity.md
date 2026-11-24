# Visual foundations and detail routes (#41, #43)

Work dated 2026-10-21. Both issues remain **open**: this is a tested native
implementation increment, with explicit reference/live acceptance gaps.

The [reference specification](../specs/spotify-desktop-parity.md) records artifact
identity, REA coverage, token provenance, native interaction policy and the
comparison matrix. Installed file/product version 1.3.5.3058 was verified
read-only; extracted XPUI is the older 1.3.4.258. Computer Use was stopped by
physical Escape before a fresh desktop capture. The requested ten-minute pause
was respected; the subsequent access attempt still reported the stop, and no
further Computer Use was attempted.

## Implemented

- Shared tokens, independently authored procedural icons, primary/secondary/
  quiet buttons, selected category tabs, track rows, artwork cards and menu
  rows. Selected tabs remain operable; disabled controls reject activation.
  Pointer capture, release outside, cancellation, keyboard focus and current
  track states share the existing bounded input machinery.
- Eleven scalable C/HLSL glyphs each use one instanced quad, without a texture
  or font dependency. Icon descriptors live in the existing frame scratch arena.
  Pixel samples at all four scales verify real glyph coverage and transparent
  corners. No proprietary font or icon asset is distributed.
- Playlist cover fallback/title/count hero, green primary Play, ordinal/title
  with artist below, album/duration columns, neutral selection and fixed table
  heading. The viewport, metadata preparation and keyboard navigation all use
  the same 56-DIP row height. Bounded indexes, copied queues and stable occurrence
  identity are preserved.
- Search selected category pills, top result and mixed rows; album/artist
  categories and artist releases use responsive artwork cards. Left/Right and
  vertical/page navigation track stable items across one to four columns and
  scroll the selected card into view. Artwork preparation covers the visible
  grid, not an unrelated linear row range.
- Larger album/artist heroes and functional primary playback actions. Large
  headings are measured against the actual available header width and trimmed
  before rasterization, preserving the fixed glyph atlas at high DPI.
- Larger bold lyric lines on a colored panel, bright consumed-frame current
  line, manual scrolling and existing five-second follow hold/resume. Shared
  line-height tokens also govern keyboard scrolling and follow offsets. No
  speculative playback clock or animation timer was added.
- Route context menus expose existing Play/Open/Go to album operations. Right
  click and the Windows keyboard context-menu command use stable route targets;
  arrows/Home/End skip disabled actions, Enter dispatches one copied action,
  Escape/outside input closes and restores focus. Navigation retires the menu.
  Disabled playback explains its reason. Icon-only primary actions have bounded
  hover hints without a tooltip polling timer.

## Checks and captures

- Isolated Debug build: `/W4 /WX`, ASan enabled; **68/68 tests passed**, including
  nine explicitly enabled GPU checks. The [full log](desktop-parity/asan-isolated.txt)
  includes the deliberate sanitizer guard tests. No tests were weakened.
  The final full-width lyric adjustment also has a
  [focused ASan/GPU run](desktop-parity/asan-final-width.txt) for catalog,
  shell, fonts and components.
  [Artwork checks](desktop-parity/asan-card-artwork.txt) also verify that large
  cards request 160-pixel decodes and retire the previous route's images.
- Existing workspace build: the full run passed **59/60**. Its already-enabled
  optional caption desktop test failed at `IsIconic(w.hwnd)` after the minimize
  press; [workspace log](desktop-parity/asan-workspace.txt). This is not counted
  as desktop acceptance or silently included in the isolated pass. Its source
  and the user's existing build options were left unchanged.
- Release compiles with `/W4 /WX`. The unchanged #35 audit passes:
  **397 C translation units, six artifacts, zero failures**;
  `spotic.exe` is **1,115,136 bytes**. See
  [release-audit.json](desktop-parity/release-audit.json). Native performance
  benchmark executables were built for the audit, not run as an acceptance claim.
- App state is **246616 bytes**, below 256 KiB. Existing 2000-frame widget/app
  and 1000-frame catalog checks remain allocation-free. The component checks
  add 2000 warm frames, all glyph bounds at four scales, selected/disabled/focus
  states and input capture. Catalog tests cover 600 navigation transitions,
  grid navigation across all widths/scales, menu pointer/keyboard dispatch,
  stale retirement and hints. Playlist tests retain the 10k index/finite queues,
  <=16 materialized rows and geometry checks at all four scales.
- Release CPU navigation benchmark passes: 10k-track filtering p95 **3.527 ms**,
  Clay layout **0.110 ms/frame**, six materialized rows and **511424 bytes** of
  model storage. This warmed disk-index workload is not a startup/idle/GPU or
  physical audio acceptance measurement.
- **80 PNG captures**: 60 base route images (five routes x three widths x four
  scales), four component showcases, and 16 Search loading/empty/error/menu
  images at the default width. The component GPU test samples actual triangle
  pixels and transparent corners, so a solid placeholder square cannot pass.

The [comparison gallery](desktop-parity/index.html) has route/width/scale/state
selectors. It distinguishes preceding native fixtures from the new native
captures and shows retained Spotify Search/Album crops separately. Those crops
have different content, viewport and unmeasured DPI; they are not pixel-parity
passes. [manifest.json](desktop-parity/manifest.json) records dimensions and
SHA-256 identities. Existing native fixture captures remain unchanged.

Reproduction from a Visual Studio Developer PowerShell:

```powershell
cmake -B build-issues-41-43 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_RENDERER_GPU_TESTS=ON
cmake --build build-issues-41-43
ctest --test-dir build-issues-41-43 --output-on-failure
& ./.cache/proto-tools/Scripts/python.exe tools/route_captures.py build-issues-41-43 docs/verification/desktop-parity
& ./.cache/proto-tools/Scripts/python.exe tools/parity_gallery.py docs/verification/desktop-parity --build build-issues-41-43 --reference-version 1.3.5.3058
```

The existing local Python environment must be selected with
`-DPython3_EXECUTABLE=...` if Python is absent from PATH. GPU tests are explicitly
opted in above. They use WARP and synthetic content; they do not establish
physical-device or live account compatibility. The user's running Release app
locked `build-rel/spotic.exe`; verification uses `build-rel-issues-41-43` and
leaves that running process alone.

## Remaining acceptance

| Area | Status |
| --- | --- |
| Exact desktop reference | Fresh measured desktop width/DPI/zoom/layout variants and keyboard timings pending because Computer Use is stopped. |
| #42 shell dependency | Current shell is retained; full reference shell/Home/Library parity is separate work. |
| Playlist artwork/metadata | Header currently uses an explicit music-glyph fallback; the disk-index adapter does not supply a cover/owner/description contract. No data is invented. |
| Artist hero | Available avatar data is displayed; it does not substitute for the observed full-width artist banner, which is not present in this model. |
| Album/disc composition | Existing API model has no disc grouping metadata; no disc headings are invented. |
| Typography | Segoe UI is the explicit system fallback for proprietary SpotifyMixUI; monochrome emoji and scalar-based text editing retain documented limitations. |
| Long lyrics | Fixed-height virtual rows trim long lines to the viewport; reference-style multiline lyric wrapping remains acceptance work. |
| Reference states | Native default route captures cover all widths/scales; additional loading/empty/error/menu captures currently cover Search at the default width. Full matching-content reference/state comparisons are pending. |
| Live routes | No new account/API probe ran. The preceding Search/Album/Artist HTTP 403 results and unaccepted Lyrics route remain blockers; Playlist has preceding live integration evidence. New contracts require REA/spec-first work. |
| Performance | Fixed-storage/warm-frame checks and the #35 source/import/size audit are distinct from fresh resident-RAM/startup/idle/physical-WASAPI acceptance. |

Neither issue should be closed from token values or synthetic screenshots alone.
