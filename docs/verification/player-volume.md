# Persistent player volume verification (#38)

Windows 11 build 26200, MSVC 19.51, C17, `/W4 /WX`, 2026-10-20.
The [contract](../specs/player-volume.md) was written before implementation.
No Spotify API or wire format was added. The optional opaque acknowledgement
ticket belongs to the local player command/snapshot handoff.

## Correctness

The full working-tree Debug/ASan suite passes all 58 tests. An isolated snapshot
of only the issue changes against the committed baseline passes all 53 default
Debug/ASan tests. The different counts reflect existing uncommitted tests and
the working tree's caption-test opt-in. All builds use strict warnings. One
intermediate run hit the pre-existing artwork worker's five-second timeout
during parallel compilation; the final runs were performed without competing
compiler processes.

Coverage includes:

- Click, continuous drag before release, release outside the slider, capture
  cancellation, Tab focus, four arrows, Home/End, percentage display and clamping.
- Rapid mute/unmute before acknowledgement, delayed/intermediate receipts,
  unchanged snapshots, external volume/mute, command backpressure and serial
  exhaustion. Repeated snapshots never submit volume commands.
- Same-value receipt notification through the player event and the headless UI
  callback, without a polling deadline. Muted PCM output remains all zero.
- Settings/bar consistency, persistent controls during navigation, signed-out
  and unbound behavior, compact/wide geometry and 2000 allocation-free layouts.
- Local cache v3 roundtrip and invalid remembered levels, migration from v1/v2,
  muted restart with the remembered nonzero level, and applying saved volume
  before accepting subsequent user volume changes.

The fixed UI state remains under its 256 KiB compile-time cap. The slider uses
existing caller-owned widget storage. Commands are submitted during preparation;
rendering enqueues bounded actions and never calls the player service directly.

## Visible Release app acceptance

The explicitly enabled `app_volume_native` harness launches the actual Release
`spotic.exe` using `--demo --benchmark=<isolated mapping>`, with a real visible
DX11 window. It injects real Windows pointer and keyboard input, checks both the
intended UI value and the production player's authoritative snapshot, and
requires balanced shutdown. The isolated Release run passes all 11 selected
volume/player/persistence/SMTC/Connect, dependency and UI idle checks.

The harness changes volume while the drag is still held, mutes and restores the
last nonzero level, exercises all requested keys, browses to Settings, checks
compact controls, closes the process, and relaunches it to verify the saved
24% preference reaches the new player. Its preference file is the benchmark's
separate `.cache/release-bench.state`, not the user's account settings.

These are native Release GUI observations with synthetic local media and a fake
audio output device. They do not certify a Spotify account, a physical speaker,
or a second Connect device. Those account/device tests were not enabled. Existing
Connect commands continue to publish authoritative snapshots; UI integration is
covered with external commands through the real player. SMTC currently has no
volume action, and no unsupported mapping or global device-volume API was added.

Lossless client screenshots at actual 96 DPI (not rescaled):

- [Default playback bar](player-volume/default-37.png), 1084 x 681.
- [Muted, with Unmute button](player-volume/muted.png), 1084 x 681.
- [Settings and player bar agree](player-volume/settings-58.png), 1084 x 681.
- [Compact two-row controls](player-volume/narrow-24.png), 404 x 681.
- [Volume restored after process restart](player-volume/restart-24.png), 404 x 681.

The isolated executable is 999,936 bytes. Its exact identity and static resources
are recorded by [REA](../specs/player-volume-release-rea.json); dependency checks
remain in the existing Release audit. The benchmark diagnostic ABI advances to
v2 for read-only volume/control geometry fields; production invocation without
`--benchmark` opens no diagnostic mapping.

## Reproduction

Use an x64 Visual Studio developer shell with the pinned Python prerequisites:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DENABLE_VOLUME_NATIVE_TEST=ON
cmake --build build-rel
ctest --test-dir build-rel -R '^(app_volume_native|app_shell|core_state_cache|ui_widgets|audio_player|protocol_connect|app_media_controls|core_release_metrics|bench_ui_idle)$' --output-on-failure
```

The native opt-in moves the pointer and uses test windows, then restores the
pointer and closes both app processes on success. Full BMP captures are written
to `build-rel/volume-captures/`.

The #35 Release thresholds remain unchanged. UI idle tests require no extra
frames or periodic timers; the existing aggregate resident-memory gate is
recorded separately and is not silently bypassed by this feature.

## Unchanged #35 aggregate gate

The whole-process `bench_idle` gate still fails. The new build's first idle
sample has 59,125,760 resident bytes and 59,133,952 peak bytes on the repeat,
above the unchanged 20 MB limit. Both phases have no added renders/presents or
timer timeouts and CPU is below 0.05%; the idle phase records two additional
non-render message wakes, so its strict `quiet` result is also false. Paused is
quiet and shutdown is balanced. Both observed runs are retained:
[first](player-volume/bench-idle-first.jsonl),
[repeat](player-volume/bench-idle-rerun.jsonl), with their
[first](player-volume/idle-first-ctest.txt) and
[repeat](player-volume/idle-rerun-ctest.txt) CTest reports.

A controlled [parent-build comparison](player-volume/bench-idle-parent.jsonl)
used the already-built #37 executable and its matching benchmark, with the same
narrow Settings window and equivalent preferences serialized as v2. That build
reproduces the same three idle wakes (one diagnostic plus two non-render wakes),
one paused diagnostic wake, no renders/timeouts during either interval, and
balanced shutdown. Its identity matches the existing
[REA receipt](../specs/window-caption-release-rea.json), SHA-256
`cc660e97eb7aca1a769d8db9452b63df58b4a7ccbe418cbf536eb6c22590d6dd`.
This comparison does not identify the underlying OS message types or certify
the aggregate gate; it establishes that this wake pattern is present before #38.

Final reports: [working-tree ASan](player-volume/asan-ctest.txt),
[isolated ASan](player-volume/isolated-asan-ctest.txt),
[isolated Release](player-volume/release-ctest.txt),
[dependency audit](player-volume/release-audit.json).
