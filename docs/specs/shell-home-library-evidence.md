# Shell, Home and Library reference contract (#42)

Recorded 2026-10-20, before the #42 screen implementation. This document
separates packaged observations, derived estimates, native implementation
choices and unobserved runtime behavior. It does not establish visual parity.
No Spotify process, account session, browser scenario or GPU test was launched
for this investigation.

## Reference and acquisition

The reference is the installed package pinned by
[the #41 specification](spotify-desktop-parity.md): Windows x64 Spotify
**1.3.5.3058**, executable SHA-256
`4704c2b7cd76878ab36656f09989cfa132a02f9711d7a8707750a8ff68722136`,
`xpui.spa` SHA-256
`79d933e844f55494dde075f85197d6c2a6c1dffaf88ef8769af0a33d232b45ec`.
These executable/archive identities are carried forward from #41; the
executable was not reopened here. This is not a claim about the latest release.

The supplied extraction at `../issue-41/.cache/xpui-current` was inventoried
before selecting files. REA MCP `analyze_javascript_application` analyzed:

- `dwp-top-bar.js`, `dwp-home-header.js`, `dwp-home-chips-row.js`
- `home-v2.js`, `home-v2-section.js`, `home-subfeed.js`
- `xpui-routes-your-library-x.js`
- `dwp-offline-empty-state.js`, `dwp-page-error-template.js`

The complete retained analysis is
`ev_591ff2bc2f44e77aeb27efdf4c6d23eca9c048aa0a27f332ecffbc9cbc1c05ed`;
its [summary receipt](shell-home-library/rea-selected-summary.json) is
`ev_a2fa132f76945a1d45e37f8b8dba6336f40b2ff655a283832c0ddee1fedc2acc`.
REA read 241,222 bytes across nine JavaScript files, with zero parse failures.
Its extraction identity is
`e28bb0925187d3eb08606ebd8369d1c5b0a7259229e019526c6e5a52426ceef8`.
That directory identity is different from the archive digest.

Coverage is partial: 264 inventoried relevant files were unselected; dependencies
were not implicitly analyzed. The semantic graph reached its per-file/global
node ceilings and reports 18,575 unknowns. The recovered graph cannot establish
complete application behavior. REA's bounded literal trace of
`HomeGenericSectionData` found no graph entity; this is explicitly **not**
evidence of absence. The literal occurs in the byte inspection below.
See [the trace receipt](shell-home-library/rea-home-trace.json).

REA module inspection also pinned `dwp-top-bar.js` to SHA-256
`780e732beadbfb9c5c982822589a32b38a3edccb63d0a777f914577a28fc9f5c`;
see [the identity receipt](shell-home-library/rea-topbar-identity.json).
Static JavaScript was parsed as inert text and never executed.

The [packaged observations](shell-home-library/packaged-observations.json)
supplement REA with exact CSS rules and short JavaScript snippets, member hashes
and UTF-8 byte offsets. The adjacent [extraction script](shell-home-library/extract.ps1)
reproduces those observations from a supplied local extraction. Its string/rule
inspection does not resolve imports, evaluate the CSS cascade or read runtime
computed styles. Repeated selectors and surrounding conditions remain relevant.
The historical `ref/xpui` version 1.3.4.258 is a separate reference; no current
layout fact below is inferred from that older version.

## Shell observations

The following offsets are in `xpui-snapshot.css`, SHA-256
`84fcb28a2bf449e9dba5091866747e0f91af5797af0edb773cf5b3cb5ccfd987`.

| Packaged observation | Exact location | Interpretation boundary |
| --- | --- | --- |
| Root grid has top-banner, global-nav, left-sidebar/main-view/right-sidebar, and now-playing-bar areas | `.LiEjwLua6RNBCvlysAT2`, byte 16150 | Static region ordering; active panels can differ |
| Root framing is black; padding and gaps use `var(--panel-gap)` | Same rule | The active numeric gap was not resolved |
| Left sidebar width uses `calc(var(--left-sidebar-width) * 1px)` | `.yQCyOSVcMCen_NJKPHB6`, byte 17086 | Resizable property, not a measured user width |
| Registered initial left-sidebar-width is 232 | `@property`, byte 6515 | CSS initial value, not proven runtime default/minimum |
| Library panel and main panel have 8px corners and use background-base | `.u3xv0vOK9EJmOplXOrhG`, byte 18971; `.R1kU6b2KxZc8FWfbYf3m`, byte 19310 | Applies to these selectors, not every experiment |
| Player grid area is separate, black, with panel-gap padding | `.XvuTwlgQRqJ5NnVLN9ZH`, byte 21498 | Supports a persistent player boundary; does not measure height |
| Global-nav region height is `48px + 2 * panel-gap` | `.Ho539Cb0LwVdYEIDfHCW`, byte 73644 | Cannot reduce to a fixed total without the gap |
| Centered region uses a 50%-width child with 350px minimum and 546px maximum | `.wPTWE6ubGXmX2aeVyhzW`, byte 74978; `.oxFroaXCJTMXfoZDaaf1`, byte 75132 | Candidate central search-region geometry; exact DOM role is unresolved |
| An alternate class removes the central child's minimum and uses 100% width | `.swoq8h_oER0f7W3oblBE .oxFroaXCJTMXfoZDaaf1`, byte 75315 | Activation condition is unknown |
| A named search-collapse selector hides a navigation child | `.forceExpandSearchInput.searchInputCollapsed ...`, byte 76129 | Existence of a collapse variant; no measured breakpoint |
| Resize handle uses panel-gap width and a 150ms opacity transition | `.LayoutResizer__resize-bar`, byte 10808 | Pointer/keyboard resizing behavior remains unobserved |

`dwp-top-bar.js` is the **route content header**, not sufficient evidence for
the global navigation bar. At byte 2962 its exclusion list contains `/`,
`/home/`, `/search` and related routes. Its 600/1024 breakpoint map must not be
reused as global shell breakpoints. Likewise the 768px medium typography override
in #41 is a token condition, not an observed shell-collapse threshold.

Header Home/back/search/profile ordering, account controls, caption reservation,
sidebar minimum/maximum/collapsed widths, exact gaps and actual resize thresholds
remain runtime unknowns. The package bootstrap references externally supplied
`__webpack_modules__`; inspecting a lazy chunk does not resolve every shared
module or the full shell composition.

## Library observations

The JavaScript member is SHA-256
`2c808dbfb30b4403b4540d4088048135463556520ed07a90ea7ebd3f855551c6`;
the CSS member is SHA-256
`0382bd3aaeda4b779f043e065cff058f93147bfce46f5d9fa90d0a73b02dab83`.

| Packaged fact | Source and offset | Consequence for the native contract |
| --- | --- | --- |
| State labels distinguish collapsed, compact/default grid, compact/default list, and expanded variants | JS byte 130527 | Model view mode and collapse independently; do not pretend a single list reproduces every state |
| Collapse control invokes the supplied toggle and switches accessible open/collapse labels | JS byte 92436 | Provide an explicit collapse action and a meaningful accessible name |
| Row imagery is requested at 48px with circular/rounded shape variants | JS byte 48019 | Preserve aspect ratio and shape; a collapsed rail still needs meaningful artwork |
| Row image play overlay is conditional on toggle-play capability and not collapsed | JS `isCollapsed:g` snippet and byte 48019 | Unsupported entities must not expose a false play action |
| Default row interior is 48px; compact interior is 24px; a descendant has 8px padding, with compact padding-block 4px | CSS bytes 5570, 5693, 5897 and compact descendant rule | 64px default / 32px compact total density is an **estimate** also supported by placeholder pitch, not a captured row bound |
| Placeholder stripes repeat at 64px default / 32px compact pitch | CSS bytes 6284 and 6547 | Corroborates density estimate; placeholders are loading treatment |
| Header has 16px/16px/8px padding and an 8px gap | CSS byte 12533 | Reuse measured spacing where this header variant is represented |
| Expanded list has addedAt/lastPlayedAt columns, each 20%, with 24px gaps | CSS byte 6789 | These fields require actual data; do not fabricate them |
| Row play overlays become visible on hover or focus-visible | CSS bytes 8463, 8566, 8740 | Keyboard focus must expose the same available action |
| Grid play affordance appears on hover/focus-within, with 8px inset and a translated/opacity transition | CSS bytes 9207, 9704 | Preserve affordance access; respect native reduced-motion settings |
| Grid uses an estimated maximum item height of 250 | JS byte 92315 | Virtualization hint; it is **not** a measured card height |
| Sort/view picker opens through its click handler or ArrowDown, and declares combobox popup semantics | JS byte 121357 | Provide keyboard access to supported view/filter choices |
| Below 400px of the named ylx-contents container, visible text search hides the sort label | CSS byte 20459, enclosing `@container ylx-contents (width<400px)` | This local container rule is not a whole-window breakpoint |
| View menu uses four equal slots and 32px-high buttons | CSS bytes 21038, 21658 and adjacent child rule | Matches the four list/grid density variants; exact popup placement still needs observation |

The general library stylesheet also includes import dialogs and other layouts.
Its unrelated four/two/one-column modal media queries must not be presented as
the normal Library grid breakpoints. Active sorting defaults, selected-row
gestures, scroll anchoring, keyboard behavior beyond the observed callback,
drag ordering, and enabled entity filters remain unobserved.

## Home and state observations

`dwp-home-header.css` defines a 256px absolute gradient/noise background and a
conditional hidden class. The corresponding JavaScript supplies a dynamic
background color. This is background artwork, not a 256px global header.
Do not copy proprietary background/cover assets into the client.

`home-v2-section.js` (SHA-256
`eb5c8c216aabd7319c5183b9b0d9d7e7bf50977019bb55b8b43c0174e22bf7eb`)
contains separate generic, low-density grid and music-video branches beginning
at byte 10431. Titles come from `section.data.title.transformedLabel`; children
come from section items. The full-page section's loading count of ten at byte
12537 does not establish shelf columns. The imported density configuration is
unresolved in this selected-source analysis.

`dwp-home-chips-row.js`, byte 8372, explicitly stores selected-chip and open-chip
state. Which chips are supplied, their labels and whether they are available to
this account are not established. The reusable grid CSS at
`dwp-home-chips-row.css:12606` uses auto-fill/minmax with `--grid-gap` and
`--min-column-width`; neither the numeric variables nor the active Home shelf
composition were resolved. Native card dimensions/counts therefore remain
declared design choices until capture comparison.

The offline component has distinct download-information/disconnected content,
an icon, a titleLarge heading, bodyMedium secondary text, and `dir="auto"`.
The generic page-error template has an icon, heading, body and refresh action.
These packaged components establish categories and content hierarchy, not the
account's live loading/error/empty state or exact localized copy.

Signed-out Home/Library, personalized Home availability, current profile data,
empty-result content and live artwork failure treatment were not observed.
The existing native personalized-Home 403 fallback and playlist-only Library
scope must stay explicit. Showing fixture recommendations or unsupported
album/artist/library filters as live data would not satisfy #42.

## Implementation boundary and proposed contract

This is a proposed native subset derived from the facts above; chosen values
without a measured source must be documented separately in the implementation:

1. Keep shell navigation, library panel, main scroll area and player area as
   separate layout regions. Reserve existing native caption bounds before
   assigning header controls. Use #41 tokens and 8-DIP panel corners.
2. Give Library independent collapsed/list/grid state. Preserve focus and selected
   identity by item key, not recycled row position. Keep search/filter meaning
   within the actually supported playlist data.
3. Give Home data-driven headings and dense visible cards when real shelf data
   exists. Keep fallback status visible when personalized data is unavailable.
   A native minimum card width or row count is an implementation choice until
   its reference counterpart is measured.
4. Feed caller artwork through the asynchronous bounded cache. Clip/cover-crop
   it, show deliberate loading/error placeholders, reject stale textures, and
   cancel work for pages no longer owned by the active screen. Preserve visible
   range limits rather than requesting every item.
5. Expose available play/navigation actions on both hover and focus. Keep
   unsupported actions absent or clearly disabled. Apply existing reduced-motion
   and event-driven-rendering rules.
6. Represent loading, loaded, empty, offline, signed-out and error separately.
   These states must not replace immutable page ownership or hide capability
   failures behind fixture data.

## Pending acceptance evidence

All runtime screenshot fields are currently **unobserved**: physical client
size, logical viewport, OS DPI, Spotify in-app zoom, effective theme/language,
account state, experiment/feature variants, control positions, focus state,
scroll state and screenshot timestamp/method. There are no reference/native
side-by-side captures in this investigation.

Required follow-up after explicit runtime/account/GPU opt-in: narrow/default/wide
captures at 100/125/150/200% DPI; expanded/collapsed Library list/grid; all six
content states; keyboard navigation; long/RTL names; real artwork and live
Home/Library. Pin the version/hash again if the installed package changes.
Measure widths immediately on either side of each observed layout transition.

Passing deterministic or fixture tests cannot close these visual/live acceptance
gaps. Keep the #35 performance gate and all unverified discrepancies explicit.
