# Spotify desktop visual reference (#41, #43)

Reference record started 2026-10-21. This document distinguishes artifact facts,
desktop observations, and implementation policy. Neither reference is described
as the latest Spotify release.

## Reference identity

- Historical extraction: Windows Spotify **1.3.4.258**, `ref/manifest.json`;
  `xpui.spa` SHA-256 `19be39b9f8da51a5e766699bc0e7a8bf6dc6f09b1b733a2d8feb4cb2e1ce0608`.
- Installed desktop: file/product version **1.3.5.3058** was rechecked read-only
  on 2026-10-21. The preceding 2026-10-20 inspection recorded the same build
  at 1600 x 900 pixels. Its DPI was not measured. Retained public-pane captures
  are under `docs/verification/detail-routes/reference-*-main.png`.
- Fresh inspection on 2026-10-21: app discovery/launch succeeded, then the user
  stopped Computer Use and requested ten minutes without app interaction.
  After the requested pause, access still reported a physical Escape stop. No
  further Computer Use was attempted. New desktop measurements and runtime
  variants are pending; retained captures do not establish exact current
  breakpoints or keyboard behavior.
- REA analyzed the four playlist/search/album/artist scripts, without execution.
  Evidence `ev_6a69558d1451ddf32eb5bcb1db91fd2d5f7fb3195422da8a3ba63fbe37280fc8`;
  tree digest `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
  Four scripts parsed; 267 sources unselected; semantic coverage is partial.
  This is static evidence, not a runtime behavior or API compatibility test.

## Artifact-derived foundations

`ref/xpui/xpui-snapshot.css`, SHA-256
`fdc9112f7a179e06889ddf3f1f2e9d2c3f2f3860339b17b6b4bae03affcebea5`,
contains the following Encore dark base set. Hex values are sRGB.

| Role | Value |
| --- | --- |
| Canvas / pressed base | `#000000` |
| Panel | `#121212` |
| Highlight / elevated panel | `#1f1f1f` |
| Elevated highlight | `#2a2a2a` |
| Primary text | `#ffffff` |
| Secondary text | `#b3b3b3` |
| Subdued essential | `#7c7c7c` |
| Accent | `#1ed760` |
| Negative text | `#f3727f` |
| Warning text | `#ffa42b` |
| Spacing | 4, 8, 12, 16, 20, 24, 32, 40 DIPs |
| Type | 11, 13, 16, 18, 20, 24, 32 DIPs (rem base 16) |

The extraction declares SpotifyMixUI. It is proprietary reference material and
is **not redistributed or loaded** by the client. Segoe UI/DirectWrite system
fallback is the explicit native approximation; shaping, ligatures, Unicode and
RTL use DirectWrite. Icon geometry is independently authored procedural C/HLSL,
kept as original project source; no extracted icon/font asset is shipped. The
repository currently has no top-level distribution license; this work does not
grant a license for Spotify's reference materials.

## Native component contract

All geometry is in DIPs and scales once in the renderer. Shared components use
the tokens above. Primary actions use a green circle or pill with dark glyphs;
secondary buttons use neutral pills; active category tabs use white with dark
text. Selected and disabled are independent states. Rows have transparent base,
neutral hover/selection, green current-track text, and a distinct focus ring.
Focus is never indicated by color alone. Disabled actions cannot capture input
or activate through Enter/Space, and their reason must be provided by the host.
Menus use elevated panels, 4-DIP corners and consistent row targets. Status
messages retain the readable wrapped text and keyboard detail dialog from #40.

Native keyboard policy: Tab/Shift+Tab traverse enabled controls; Enter activates;
list arrows/Home/End/PageUp/PageDown select stable occurrences; Escape cancels
capture/popups; Alt+Left returns through route history. This policy is tested
locally and is not yet claimed as exact Spotify keyboard parity. Text editing
is Unicode scalar based; grapheme/bidi visual caret movement remains limited.
No transition animation is necessary: reduced-motion behavior is the default.

## Route targets and comparison matrix

Capture the same public content and player state in each app. Record executable
version, date, physical viewport, `GetDpiForWindow`, app zoom, theme, library and
right-panel variants in a manifest. A mismatch in content or dimensions must be
labeled; it cannot count as a pixel comparison. Never capture account details.

| Surface | Required states | Concrete composition target |
| --- | --- | --- |
| Shell/Home/Library (#42) | narrow, normal, wide; selected/hover/focus; empty/loading/error | 8-DIP panel gutters, rounded dark panes, persistent player; measure full desktop before parity signoff |
| Playlist (#43) | cover, metadata, default/filtered/sorted, selected/current, scroll, unavailable | cover/title/metadata hero; primary Play; ordinal/title with artist below, album/duration; fixed table heading and bounded list |
| Search (#43) | query, categories, results, empty/loading/error | selected category pills; top result and mixed rows; artwork cards for album/artist categories |
| Album (#43) | cover/title/artist, tracks/discs, continuation/error | large cover hero, primary Play, ordinal/title/duration rows; use supplied disc metadata only |
| Artist (#43) | hero, popular tracks, releases, restricted/error | supplied image in hero; Popular section; release cards; do not invent banner/follower metadata |
| Lyrics (#43) | synchronized/plain/missing/capped, manual scroll/resume | large bold text on colored panel, bright current line; consumed-frame highlighting and explicit follow control |
| Components (#41) | normal/hover/pressed/selected/focused/disabled | buttons, glyphs, cards, rows, tabs, menu/status and readable disabled reasons |

For every row capture **400/1100/1440 DIPs x 720 DIPs**, at
**96/120/144/192 DPI** (100/125/150/200%). These are reproducible native test
widths, not observed Spotify breakpoints. Compare panel/text/artwork/target bounds,
not just token colors. Current host compact breakpoint is 760 DIPs; right panel
appears at 1180 DIPs. Both remain native policies pending desktop measurement.

## Acceptance boundaries

Synthetic captures and deterministic ASan navigation tests verify local geometry,
resource retirement and input behavior. They cannot prove live routes. Existing
live Search/Album/Artist probes returned HTTP 403; Lyrics was not accepted. Keep
those failures visible and do not change internal contracts without new REA
request/header/hash/shape evidence. Full Spotify parity and issues #41/#43 remain
open until matching reference captures, desktop keyboard acceptance and live
route results satisfy the matrix. Preserve #37 caption, #38 volume, #40 notices
and the unchanged #35 release audit/performance gate.
