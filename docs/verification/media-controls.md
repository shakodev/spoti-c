# Windows media controls verification (#33)

Verified on Windows x64, 2026-10-19, MSVC C17, Ninja, `/W4 /WX` and ASan.
[REA evidence and the ABI/ownership specification](../specs/media-controls.md)
were recorded before implementation. No new Spotify request is introduced.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

All **51/51 default ASan tests pass**, with zero compiler warnings or sanitizer
errors. `app_media_controls` is deterministic and requires no GPU, audio device,
network or account. The Release app and media-controls test also build cleanly;
the targeted Release `app_media_controls` CTest passes.

The new tests exercise:

- Playback status, bounded UTF-8 metadata, missing/cleared metadata, invalid
  inputs, failed publication retry and explicit unavailable-platform cleanup.
- Audible frame-to-100 ns conversion, clamping, overflow, seek conversion,
  unknown duration and invalid timeline. Ordinary position publication is
  limited to 1 Hz; track/state/seek-generation changes publish immediately.
- SDK button/seek delegates, idempotent Play/Pause, shared SMTC/APPCOMMAND/raw
  key dispatch, cross-source duplicates, independent repeats, stale generations,
  unsupported seek and explicit queue/player FULL without losing retryability.
- Four concurrent producers filling the fixed 16-command queue, nonblocking
  event wake, failed token-removal retry and exactly-once removal/release.
- Close during command submission and close during a blocked COM callback.
  The latter poisons disposed control storage with ASan while the retained,
  detached delegate finishes. Every tracked delegate cell is released.
- Actual AppDesktop input routing, existing PlayerCommand admission, bound
  title/artist/art identity, visible admission failure and sign-out clearing.
- 2,000 unchanged publication calls with zero tracked CRT heap calls and no
  adapter publication. Native strings/art references are created only when
  metadata changes; the common dispatch/publication path has no heap allocation.

The build checks all eight private interface/delegate IID literals against
Windows SDK 10.0.26100.0. C static assertions check critical vtable slots and
TimeSpan/token layouts. Production uses the SDK's C projections directly.
`dumpbin /directives` on the three media-control objects shows C CRT/ASan and
Windows UUID dependencies, with no msvcprt/libstdc++. `dumpbin /dependents` on
the app has no C++ standard-library DLL import. Required new Windows libraries
are runtimeobject, ole32 and uuid; CMake remains `LANGUAGES C`.

Fixed application state is **253,696 bytes**: App 251,536, MediaControls 2,048
and MediaWinrt 112, below the existing 256 KiB reservation. Native subscriptions
use two bounded, independently refcounted setup cells; Windows owns its COM,
thumbnail retrieval and driver allocations. These bounds do not establish
whole-process RAM, executable size or startup performance.

## Native smoke and remaining device checks

The explicitly opted-in `build/test_media_controls.exe --native` passed under
ASan against a real top-level HWND and Windows Runtime SMTC. The separately
registered opt-in CTest passed too; the default option was restored to OFF.
It verifies native
title/artist getters, replacement-track clearing, Paused/Playing status, the
two-second audible timeline, unchanged-publication suppression, actual event
registration/removal, zero live delegate cells and apartment release before
HWND destruction. It can also be registered separately:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_MEDIA_CONTROLS_NATIVE_TEST=ON
cmake --build build
ctest --test-dir build -R '^app_media_controls_native$' --output-on-failure
cmake -B build -DENABLE_MEDIA_CONTROLS_NATIVE_TEST=OFF
```

The native module audit found **Windows' inbox System32/msvcp_win.dll** loaded
by SMTC. It logs this OS dependency and rejects other msvcp/concrt/libstdc++
modules. No application C++ source or redistributable standard-library runtime
was added, but the issue's literal "no C++ runtime loaded" condition is **not
met** by this Windows implementation. Import/link auditing and the OS module
finding are distinct results.

Visual lock-screen/system-flyout rendering, physical media keys, audible output
and default-device switching remain unverified. The pre-#33 headless physical
WASAPI attempt failed at decode/device startup despite enumerated Windows audio
devices. The native SMTC smoke requires no audio playback and does not establish
physical-device compatibility. No skipped live check is reported as a pass.
