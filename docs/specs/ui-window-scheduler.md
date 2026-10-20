# Win32 window and scheduler (#25)

Evidence recorded before implementation, 2026-10-19. This module adds no Spotify
endpoint or protocol behavior.

REA MCP `analyze_javascript_application` inspected `ref/xpui` with the exact
selection `pip-mini-player-snapshot.js`. Parent evidence:
`ev_281f5d89d1afb5eb927c32c6e9472e48c7e52f8fd1e896624e457751c0ae5c06`;
summary evidence:
`ev_237f5ec9556204d16e09fc299c7fdeb54d2a55a4fb0daf310017df3d6af14598`.
Directory SHA-256:
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
One JavaScript file parsed without failure; 270 inventoried sources were not
selected. The semantic graph reached its per-file node ceiling. No native
BrowserWindow construction was recovered in that selection; native scheduling,
accessibility and hit testing remain unobserved. This is static evidence, not a
claim that Spotify uses our Win32 implementation.

Supplemental direct reference inspection (not REA runtime evidence):

- `pip-mini-player-snapshot.js`, SHA-256
  `9260b144cd1053c23ef75f84fac6d05f7ea38cdb68848986a89cbd4d83e948eb`,
  registers `FocusMainWindowAPI`; its implementation remains external.
- `xpui-snapshot.css`, SHA-256
  `fdc9112f7a179e06889ddf3f1f2e9d2c3f2f3860339b17b6b4bae03affcebea5`,
  defines desktop `.body-drag-top` as a 60px high drag region and marks
  interactive regions `-webkit-app-region:no-drag`. These are CSS layout facts,
  not Windows pixel/DPI metrics. Our caption uses native DPI-scaled metrics.
- `ref/librespot/core/src/session.rs:683` defines network keepalive deadlines.
  They belong to the network worker, not the UI timer.
- `vendor/clay.h` v0.14 requires a caller-owned arena. Layout integration is a
  later module; this scheduler and frame callback allocate no heap storage.

## Implementation contract

The UI thread owns HWND, scheduler and callbacks. Dirty reasons coalesce until a
visible frame consumes them. Hidden/minimized or renderer-reported occlusion
suspends visual deadlines and preserves dirty state. Restoring schedules one
frame; no occlusion polling is introduced. A future DXGI renderer must report
occlusion changes with `ui_window_set_occluded` when receiving an external
visibility/presentation opportunity. Native paint, position, activation and show
messages also permit an occluded surface to attempt one new dirty frame.

Use one `MsgWaitForMultipleObjectsEx` timeout for the nearest monotonic deadline:
1000ms progress while playing, 17ms animation only during an explicitly bounded
transition, and one caller-provided lyrics boundary while playing and visible.
Late wakes skip missed frames. No recurring OS timers or timeBeginPeriod.
Rearming an identical lyrics boundary does not repeat it. Pause disarms lyrics
and progress; the playback owner recomputes the boundary on resume or seek.

The existing manual-reset EventQueue owns its event handle. Posting and
empty/reset share its lock. Drain one bounded batch per turn; remaining work
keeps the event signaled. A closed queue ends the window pump instead of spinning.
Producers must quiesce before queue destruction. Frame/worker callbacks are
nonblocking and must use preallocated storage. Dirty, wake, timeout, render and
timer counters are inspectable for tests and release benchmarks.

Keep WS_OVERLAPPEDWINDOW system semantics, DWM caption buttons, resize borders,
drag/snap, system menu and default keyboard/IME/accessibility processing.
Issue #37 restores the standard nonclient caption: the earlier client extension
allowed the opaque DX11 surface to cover its glyphs. Native nonclient layout and
hit testing replace the guessed caption geometry; see [window-caption.md](window-caption.md).
Device messages dirty one frame and notify the owner; no audio/network work runs
in WndProc.

Platform references: Microsoft's [custom frame guidance](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)
and [DwmDefWindowProc contract](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmdefwindowproc)
require DWM-first caption hit testing and forwarding nonclient mouse leave for
button hover cleanup. Native accessibility is verified through
[AccessibleObjectFromWindow](https://learn.microsoft.com/en-us/windows/win32/winauto/retrieving-an-iaccessible-object),
which exercises the window's default WM_GETOBJECT handling.

## Verification (Windows x64 / MSVC, 2026-10-19)

- Debug configured with `ENABLE_ASAN=ON`, built with `/W4 /WX` and
  `/fsanitize=address`; all 42 default CTest tests passed.
- `ui_scheduler` advances a fake millisecond clock through dirty coalescing,
  pause, hide/occlusion, late wakes, finite animation bounds and lyrics deadlines.
- `ui_window` creates a real offscreen HWND and verifies size, synthetic DPI
  changes, hit-test geometry, focus, IME dispatch, native maximize/restore/close,
  accessible window name and caption children, painting during modal resize,
  paint recovery after occlusion, 2000 concurrent worker posts and event reset
  behavior. The arena and owned HWND/event metrics balance at shutdown.
- Release `ui_scheduler`, `ui_window` and `bench_ui_idle` passed. The benchmark
  recorded an uninterrupted 3000ms visible-window idle interval: one blocking
  wait, zero frame callbacks and zero periodic timers. No polling loop is used.
- Release executable dependency inspection found C/Windows runtimes only; no
  C++ runtime. The frame sink here is a GDI placeholder. GPU Present/occlusion
  registration, multi-monitor physical DPI transitions and interactive shell
  snap-layout UI are not claimed as live GPU/hardware acceptance by these tests.

Reproduce with the issue's Debug commands, then configure `build-rel` as Release,
build `spotic test_ui_scheduler test_ui_window`, and run
`ctest --test-dir build-rel -R "^(ui_scheduler|ui_window|bench_ui_idle)$" -V`.
