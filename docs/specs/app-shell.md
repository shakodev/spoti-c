# Application shell (#30)

Evidence recorded before implementation, 2026-10-19. REA MCP analyzed the five
issue references: `home-v2.js`, `home-v2-section.js`, `9806.js`,
`xpui-routes-your-library-x.js` and `dwp-now-playing-bar.js`. Parent Evidence
`ev_5075db37c7cc49352d1bdcf998d33eb40f5eda30b5b016a001cda6d94fce66a1`;
directory SHA-256 `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
All five parsed, 355644 source bytes, zero parse failures. The other 266 files
remain outside selection. Application coverage is partial; semantic coverage
is partial/truncated at the 20000-per-file/100000-total node ceilings.

REA trace `ev_1d9b91979c6a20c334e15a0bd1fbad30b648716fddefc2b81b4214a2fb908e11`
observes bundled module 87269 exporting `NowPlayingBar` in
`dwp-now-playing-bar.js`, SHA-256
`b4685d9bb12a29e3d6cfce15203858f185e49b7d6ae170358c881ab60f2edd3a`.
The trace also reaches unresolved shared imports; containment reachability is
not proof that every reached module implements that bar. The retained library
artifact is SHA-256
`5546222725161dd414e4648e6c59ed94d25b38b9fa3802e7187ab9d855985a6c`.
Portable summary and coverage are in [app-shell-rea.json](app-shell-rea.json).
This static pass executed neither Spotify nor network requests. Exact Spotify
breakpoints, visual layout, clock ownership and navigation lifecycle remain
unproven; the rules below are native design decisions required by the issue.

Existing API authorities remain [home-library-playlist.md](home-library-playlist.md),
[pathfinder.md](pathfinder.md) and [native-library-playlist.md](native-library-playlist.md).
No new endpoint, query hash, token protocol or guessed JSON shape is introduced.

- One UI owner holds a <=256 KiB App plus widgets, 16 history entries, one
  retained immutable collection page, a copied PlayerSnapshot, selected entity
  and fixed command/input/focus/image slots. Clay+scratch retain #29's 1.25 MiB
  cap. No layout/render heap allocation, synchronous fetch or history growth.
- Route changes advance a generation, cancel the previous owner request and
  artwork generation, and release page/image references. Late deliveries are
  consumed/released and cannot overwrite a new route. Player metadata is owned
  separately from route pages. Pagination replaces the current page only after
  a valid next page arrives; errors preserve the visible page and offer retry.
- Sidebar, main viewport and the bottom playback bar have independent stable
  IDs. Below 760 DIPs the sidebar collapses; detail is optional above 1180 DIPs.
  Minimum supported client area is 400x320 DIPs. Home shelves and Library use
  bounded virtual rows and explicit page continuation; unavailable unions remain
  visibly disabled. Keyboard Tab/Shift-Tab, Enter/Space, arrows, Home/End,
  Alt-Left, scrolling and seek keys reach the same commands as pointer controls.
- Commands are queued during layout and dispatched only at the preparation
  boundary. Player actions use PlayerCommand and never decode or reset a sink.
  The latest consumed-frame snapshot drives seek/time display; the UI never
  extrapolates audio position from wall time. The scheduler arms 1 Hz progress
  only while playing. Idle and paused states have no polling deadline.
- Login/logout delegate to the configured OAuth/token/credential owner; settings
  adjust existing player volume and local detail preference. The shell never
  owns credential strings. Logout clears private page/history/selection state
  and submits STOP, with explicit pending/error outcomes from the session owner.
- Production hosts enqueue requests to their collection transport owner and
  publish retained pages back on the existing wake queue. In particular,
  API_NATIVE_RPC is not executable HTTP: a host must supply the verified local
  bridge or its own implemented service. A fixture host is provided for offline
  shell use and deterministic tests; it cannot establish live account acceptance.
- Native input reaches the app through an optional window callback; preparation
  processes bounded input and service work, then renders one final dirty frame.
  Font shaping/upload stays in preparation, and the renderer's failure fallback
  remains available. Visual QA uses exported frames at narrow/default/wide sizes;
  optional GPU checks remain opt-in.

## Implemented ownership and integration

`sizeof(App)` is 148232 bytes on the verified x64 build, including the 22432-byte
widget state, fixed word-measurement cache and all navigation/control buffers.
The cache holds scalar DirectWrite word advances separately from the existing
bounded glyph-run cache; this prevents Clay word probes from evicting visible
runs and perpetually waking an otherwise idle screen. Old scalar measurements
are replaced in place when changing progress timestamps exhaust the cache.
Text shaping and atlas
upload run at preparation boundaries. Saturated queues report FULL; input-edge
overflow collapses to explicit capture cancellation instead of a phantom click.

`AppServices` is the asynchronous host boundary. Collection deliveries transfer
one retained reference to `app_deliver`, including stale/error deliveries. The
transport owner separately retires its own service reference. Authentication
completion has a separate serial from route generations so navigation cannot
discard a pending login, while logout invalidates prior auth completions.
`app_account_action` runs on the account owner and calls existing PKCE/token/
credential APIs; the owner continues OAuth/AP/token establishment and reports
the resulting session. It must quiesce AP credential use before logout.

The standalone default intentionally has unconfigured account/collection
callbacks. `--demo` explicitly binds a serialized four-slot worker, synthetic
JSON fixtures, the existing collection parser and shared page pool, plus the
existing headless player with loopback encrypted audio and a fake output device.
`--demo-wasapi` opts into physical audio. These are development fixture paths,
not a packaged live client. The fixture pool (1 MiB) and collection model arena
(64 KiB) occupy their existing API/model budgets outside App's 256 KiB limit.
No credential strings or live endpoints are embedded in the fixture host.

The optional headless state callback publishes only semantic player changes
from the decode owner, never per-period audio callback wakes. Progress-only
snapshot changes do not dirty the UI; the existing 1 Hz scheduler drives them.
The main window delegates native input and service preparation to the same
callbacks exercised by the shell tests. [Verification and screenshots](../verification/app-shell.md)
record tested scope and the remaining live-host boundary.
