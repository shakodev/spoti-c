# Application shell verification (#30)

Verified on Windows x64, 2026-10-19, using MSVC C17, Ninja, strict `/W4 /WX`
and Debug AddressSanitizer. The [specification](../specs/app-shell.md) and
[REA summary](../specs/app-shell-rea.json) were recorded before implementation.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

All 48 default tests passed with no compiler warnings or ASan failures. The
shell test covers responsive widths/DPI, bounded history and queue overflow,
navigation/back, all data states, pagination, stale delivery consumption,
logout, real player load/play/pause/seek and consumed-frame snapshots. Across
600 Home/Library transitions there is one visible retained page and zero pages
after teardown. Artwork worker tests check coalescing, cancellation and handle
balance. Account-owner tests call existing PKCE begin/cancel and credential
logout against a temporary file, without an account or browser.

Native HWND tests use the production input callback for keyboard,
pointer, capture and focus routing. Keyboard tests exercise Tab/Shift-Tab,
Enter, Back, list navigation and seek. 2000 warm layouts and 1000 real
DirectWrite/Clay/CPU-render frames report zero tracked Debug CRT heap calls;
the latter also requires no pending text and no further shaping. A further 320
distinct playback timestamps cross the scalar-measurement cache capacity and
verify bounded eviction without a failed layout or pending-text backlog. This tracks
application heap use, not every OS/driver allocation or process-wide leaks.
App plus widgets uses 148232 bytes, below its 256 KiB cap. Final process RAM,
startup, executable-size and full-client CPU gates remain separate work.

The Release build and targeted `app_shell` / `audio_headless_smoke` tests also
pass. Physical-device and account tests were not run. The semantic playback
notification test requires multiple state edges and fewer UI notifications than
audio callback periods; the scheduler removes progress deadlines when paused.

## Visual QA

The optional real DX11/WARP path was run under ASan with
`build/test_app_shell.exe --visual` from `build/`. This exports raw RGBA frames
after font preparation and requires an empty pending-text queue. It is also
registered as `app_shell_visual` only with `ENABLE_RENDERER_GPU_TESTS=ON`.
The PNGs below are lossless conversions of those captures; all were inspected.

| Capture | Physical pixels | Client DIPs at 125% |
|---|---:|---:|
| [Narrow](app-shell-narrow.png) | 500 x 800 | 400 x 640 |
| [Default](app-shell-default.png) | 1375 x 900 | 1100 x 720 |
| [Wide](app-shell-wide.png) | 1800 x 1125 | 1440 x 900 |

The narrow sidebar collapses, shelf columns adapt, and the playback bar stays
below the independent main viewport. The wide capture enables the optional
detail pane. Text and controls remain clipped to their pane bounds. Artwork
placeholders are intentional in the offline fixtures. The screenshots show
synthetic Home data and do not establish Spotify visual parity.

## Running and hosting

`build/spotic.exe --demo` uses synthetic paged Home/Library data and the existing
headless encrypted-audio loopback fixture with fake output. Select a playable
track on Home or its shelf page to start it; use Tab/Enter, pointer controls,
Back/Alt-Left and Settings for navigation, playback and volume. Use
`--demo-wasapi` only when audible synthetic output is desired.

The default executable is signed out with no live transport configured. A live
host must enqueue collection/account requests through `AppServices`, publish
immutable pages and player snapshots, and connect the already documented native
Library RPC bridge. The shell does not invent an HTTP replacement for that
bridge. `app_account_action` is an account-owner helper, not a blocking UI
callback. Live login, account browsing and packaged fixture deployment remain
outside this fixture-driven shell verification.
