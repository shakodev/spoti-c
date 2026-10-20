# Spotify desktop reference and shared foundations (#41)

Evidence recorded before UI implementation, 2026-10-20. This is a reference
contract for #42 (shell/Home/Library), #43 (detail screens), and #44 (player).
Static package observations, runtime observations and local implementation
choices are different evidence categories. Matching tokens alone does not
establish pixel or interaction parity.

## Reference identity and evidence

The installed Windows x64 `Spotify.exe` reports **1.3.5.3058**, SHA-256
`4704c2b7cd76878ab36656f09989cfa132a02f9711d7a8707750a8ff68722136`.
REA MCP `inspect_pe_resources` receipt:
`ev_ff6faa44810f4f10187b9cd80c2823fe80880f8e7006d7743fbd61ff178ca56a`.
It inventories 15 resources in a 4,001,152-byte PE32+ AMD64 executable; it does
not observe rendering. Version information is read separately through Windows.
The installed `Apps/xpui.spa` has SHA-256
`79d933e844f55494dde075f85197d6c2a6c1dffaf88ef8769af0a33d232b45ec`,
581 regular members. This identifies the installed reference, not the latest
Spotify release worldwide.

The existing `ref/xpui` is historical: the repository manifest associates it
with **1.3.4.258**, archive SHA-256
`19be39b9f8da51a5e766699bc0e7a8bf6dc6f09b1b733a2d8feb4cb2e1ce0608`,
574 regular members. REA MCP analyzed `dwp-top-bar.js`, `dwp-home-header.js`,
and `dwp-now-playing-bar.js` in both trees:

| Tree | REA parent Evidence | Selected JavaScript |
| --- | --- | --- |
| Historical | `ev_578397af6bc1bc0bfdee358cf41379f4922d1bf9419bd9094938269a54cfc169` | 3 files, 59,115 bytes, no parse failures |
| Installed | `ev_328285bdc75eedff02287b1b870c929e753b5f9160642e3522dca4441aa8e064` | 3 files, 62,607 bytes, no parse failures |

Coverage is explicitly partial: unselected modules and dynamic relationships
are unknown. No JavaScript was executed by these static inspections.
REA archive extraction rejected the package's `/` directory entry.
`tools/parity_reference.py` therefore skips directory entries, validates every
regular file path, extracts into a fresh local directory, and records archive
and member SHA-256 values. REA then analyzes that extraction. Its CSS parser
records exact selectors and UTF-8 byte offsets as supplemental static evidence;
it is not a browser cascade evaluator.

## Packaged visual measurements

These measurements come from the installed package's `xpui-snapshot.css`.
Values below are CSS logical pixels at a 16px root; native DIP is the intended
equivalent. Active account experiments and container variants still require
runtime corroboration.

| Property | Exact packaged value / location |
| --- | --- |
| App frame | `#000000`, `.encore-app-frame-set` |
| Panel | `#121212`, base dark set, byte 302393 |
| Elevated / hover / elevated press | `#1f1f1f` / `#2a2a2a` / `#191919` |
| Main / secondary text | `#ffffff` / `#b3b3b3` |
| Accent / accent hover / accent press | `#1ed760` / `#3be477` / `#1abc54` |
| Negative / warning / announcement text | `#f3727f` / `#ffa42b` / `#539df5` |
| Essential subdued / decorative subdued | `#7c7c7c` / `#292929` |
| Tinted base / hover / press | white alpha `1a` / `24` / `36` |
| Small spacing | 2, 4, 6, 8, 12, 16 |
| Medium-device larger spacing | 24, 32, 48, 64, 96, 128 |
| Control heights | 32 / 48 / 56 |
| Corner radii | 2 / 4 / 6 / 8 / 16; pills 9999 |
| Icon canvas sizes | 12 / 16 / 24 / 32 / 40 / 48 / 64 / 88 |
| Focus border | 2 |
| Overlay shadow | `0 4px 12px 0 #0000004d` |
| Disabled / active opacity | .3 / .7 |
| Hover scale | 1.04 |
| Short durations | 50 / 100 / 150 / 200 / 250 / 300 ms |

Spacing/radius/control definitions are at bytes 329466 and 333557; the latter
is the medium-device variant. Typography classes at bytes 501764–503710 use
weight 400 for body and 700 for bold/title, 800 for the largest headline.
Medium-device roles are body 16, small 14, marginal 12, title 24/32/48,
headline 64/96. The small-device variant uses small 13, marginal 11,
title 20/24/32 and headline 40/48. The surrounding media-query condition must
be retained when using these values; a repeated selector does not mean an
unconditional override. The default medium override is inside
`@media (width>=768px)`; `.encore-medium-devices-theme` also sets it explicitly.
This token threshold is not an observed shell-layout breakpoint.

The source font stacks name SpotifyMixUI, SpotifyMixUITitle and script-specific
CircularSp faces (Arabic, Hebrew, Cyrillic, Greek, Devanagari). **Do not bundle
these proprietary font files.** Native text uses the installed Segoe UI family
with DirectWrite fallback and shaping; this is a declared font-metrics fidelity
gap. Per-label normal/bold and RTL configurations must use identical cache keys
for Clay measurement and rendering.

`dwp-now-playing-bar.css` records Windows panel radius 8 (byte 366), the small
transport circle 32x32 (byte 3932), and a selected 4x4 `#1db954` indicator
(byte 2268). These selectors do not prove that every variant is enabled in the
running app. Runtime shell geometry, artwork variants, scrollbar width,
tooltip timing and menu placement remain unmeasured.

## Native component contract

Implement immutable semantic tokens, explicit normal/bold typography roles,
and reusable button/icon-button/card/row/tab/menu/status primitives.
Geometry stays in DIP; the renderer applies 96/120/144/192 DPI exactly once.
Icons are original procedural geometric drawings on a 24-unit canvas, not
Unicode font characters, copied Spotify SVG paths, or redistributed artwork.
Navigation arrows mirror in RTL; transport/media semantics do not.

Normal, hover, pressed, selected, disabled and keyboard-focus states must be
distinguishable. Accent controls use black text; neutral controls use white or
subdued text. Focus uses a 2-DIP ring independent of hover. Selection uses
fill plus icon/indicator, not color alone. Label storage is caller-owned.
Artwork supplied by callers is clipped, never stretched to a different aspect.
The showcase uses original procedural artwork with a recorded source.

Buttons activate on release over the captured control or Enter/Space while
focused. Disabled controls cannot activate. Tab/Shift+Tab skip disabled items.
Tabs and menus support arrows and Home/End; menus additionally support Escape,
Enter/Space, disabled-item skipping, and restoration of the opener's focus.
Directional navigation follows the layout direction. These are native Windows
interaction requirements pending comparison with the reference runtime.

Reduced motion disables interpolation and recurring animation deadlines.
The implementation reads the Windows client-area-animation preference outside
rendering and refreshes it on settings changes. No new idle polling is allowed.
Control state, icon descriptors, menu storage and frame scratch are bounded.
Warm layout/render and audio callbacks must not allocate.

### Implemented foundation and remaining fidelity differences

`ui_theme.c` stores the medium-device dark tokens above. `components.c` uses
caller-specified sizes, bounded Clay clips, and six explicit state flags.
`menu.c` borrows at most 16 items and restores focus on dismissal/activation.
`ui_focus_move` provides Tab/Shift+Tab, directional and Home/End navigation;
screen owners must route their native input events to these APIs.
`font_roles.c` resolves the same role, weight and RTL keys for Clay measurement,
preparation and rendering. Legacy font IDs remain supported.

The following geometry is an **implementation choice awaiting reference
comparison**, not a measured Spotify component shape:

| Primitive | Native contract |
| --- | --- |
| Button / tab | Pill corners; centered label/icon; clipped to caller bounds |
| Icon | One analytic quad; original 24-unit geometry, aspect fit; 17 glyphs |
| Row | 12-DIP horizontal padding, 12-DIP icon gap, 4-DIP corners |
| Card | 12-DIP padding, 8-DIP gaps/corners; title + secondary label |
| Artwork | Borrowed texture, centered cover crop; 4-DIP card corners |
| Menu | 4-DIP inset/corners, 40-DIP rows; caller controls placement |
| Status | 48-DIP height, 24-DIP icon, 8-DIP corners |

Icons and fixture artwork were authored for this repository using geometric
primitives. No third-party icon pack, Spotify SVG, proprietary font, cover
image or extracted package member is linked or distributed in the client.
Existing dependency source/license manifests remain unchanged.
Icon silhouette fidelity still requires reference comparison.

Long labels are clipped, not ellipsized by these primitives. Hover scaling,
overlay shadows, tooltip presentation, popup dismissal outside the menu, full
screen-reader integration and screen-specific scrollbar behavior are not
implemented here. The current showcase is a native diagnostic fixture, not
a recreation of a Spotify screen. The checked-in
[verification record](../verification/spotify-desktop-parity.md) distinguishes
passing foundation tests from pending visual acceptance.

## Screen/state comparison matrix

| Child | Required reference states | Concrete target |
| --- | --- | --- |
| #42 | shell, Home loaded/empty/error, Library expanded/collapsed/list/grid | measured panel edges/gaps, top navigation, cards/rows, search/focus, scrollbars |
| #43 | playlist/album/artist, selected/hovered track, context menu | artwork crop/radius, heading hierarchy, row height, columns, menu/tooltip geometry |
| #44 | stopped/playing/paused, mute/unmute, compact player, queue | transport icon geometry, progress and volume track/thumb, percentage, focus |
| #41 | component showcase normal/hover/pressed/selected/disabled/focus, RTL | shared token/state geometry and native pixel captures at all four scales |

Comparison records must include exact app version and hashes, UTC date,
physical client size, logical viewport, OS DPI, in-app zoom, theme, language,
visible feature variants, account/service availability, and capture method.
Store a reference image, native image, geometry/state JSON and a side-by-side
comparison for each supported scale (100/125/150/200%). Do not resize a 100%
capture and label it a native high-DPI observation. Compare flat-color pixels,
panel/control/icon bounds and state transitions separately from font pixels.
Font rasterization tolerances cannot hide wrong wrapping or missing glyphs.

Responsive thresholds must be measured while holding DPI and app zoom fixed:
record widths immediately before and after each change, sidebar min/max,
content columns and player-control wrapping. Until observed, retain existing
native compact layout and volume behavior; do not label its 760-DIP threshold
a Spotify breakpoint. Preserve native caption controls, message handling,
volume command/snapshot/persistence semantics and the unchanged #35 gate.

## Runtime verification status

Pending read-only reference captures and explicitly enabled GPU tests.
The Windows Computer Use helper currently reports that its native pipe is
unavailable. Static package evidence cannot substitute for shell/Home/Library/
detail/player/menu screenshots, runtime breakpoints, tooltips or keyboard
observations. Account/service-dependent gaps must remain explicit in the
verification report and the issue must not be closed as full parity on static
evidence alone.
