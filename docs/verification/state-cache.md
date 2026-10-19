# State cache and offline startup verification (#34)

Verified 2026-10-19 on Windows x64, MSVC 14.51 C17, Windows SDK 10.0.26100.0,
Ninja, `/W4 /WX`. The [specification and REA receipts](../specs/state-cache.md)
were recorded before implementation. No new Spotify endpoint was added.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

All **52/52 default Debug ASan CTests pass**, without compiler warnings or
sanitizer errors. `core_state_cache` covers exact round trips including maximum
capacity, every truncation of a valid snapshot, 10,000 deterministic mutations,
CRC failure, header/offset/count overflow, unsupported versions, v1 migration,
invalid UTF-8, unterminated labels, stale epochs and safe failure defaults.
Disk tests verify cancellation after flush, replacement failure with the old
file locked, concurrent whole-file commits and reads, and killing a separate
writer between flush and rename. The final snapshot remains valid; an abandoned
temp is ignored and then removed by the test. Worker saturation returns FULL;
save completion and shutdown release the worker. Process handle counts return
to their baseline after first-use thread/process runtime initialization.

Offline shell tests inspect actual Clay text commands for saved library labels
without invoking auth/network services, reject playback while signed out,
verify zero tracked CRT heap calls during layout, and clear retained identities
on explicit sign-out. The actual GUI probe additionally verifies useful DX11
presentation, deferred-service ordering and clean shutdown without an account,
network transport or physical audio device. The existing default host still
does not configure a live account/collection transport.
The Release probe harness also completed all 21 launches against the Debug ASan
GUI executable with clean exits and no sanitizer errors; those instrumented
timings are excluded from the Release performance results below.

Normal launches use `%LOCALAPPDATA%/spoti-c/shell.state`; the directory and save
worker are created after the first useful frame. Only the bounded snapshot is
read before window creation. Layout/render never perform cache I/O. Save
admission is limited to meaningful changes at most once per two seconds of
preparation wakes, plus final shutdown; there is no extra idle polling timer.
Window/layout, compact library IDs, last route/track, volume and cache epoch
persist. Repeat/shuffle fields are bounded retained preferences reserved for
the player host; no playback automatically resumes. No auth token, reusable
credential, key, URL or artwork bytes belong to this format.

## Native Release measurement and remaining target

Reference machine: Gigabyte B450 AORUS ELITE, AMD Ryzen 5 2600X six-core CPU,
17,095,331,840 bytes reported physical memory (approximately 16 GiB),
Windows 11 Pro 10.0.26200. QPC frequency: 10 MHz. This is a real desktop launch
measurement, not a codec microbenchmark. GPU/desktop access is explicitly opted
in and absent from the default ASan suite:

```powershell
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DENABLE_STARTUP_NATIVE_BENCH=ON
cmake --build build-rel
ctest --test-dir build-rel -R '^(core_state_cache|bench_startup)$' --output-on-failure
```

Both Release tests pass. The benchmark records OS process creation correlated
to QPC, actual WinMain entry, window creation, the first successful present with
prepared text, deferred-service start and online-ready. One warmup launch is
excluded from 20 warm-file-cache samples. Cache condition is a valid synthetic
offline snapshot, and every child runs the actual application startup path.
[Raw samples](state-cache-startup-samples.txt) retain each milestone and miss.

**The <30 ms target is missed in all 20 samples:** process creation to useful
present is **p50 315.146 ms / p95 332.182 ms** (nearest-rank percentiles).
The benchmark succeeds when it obtains valid measurements and a useful frame;
it reports target misses explicitly rather than converting them into a timing
acceptance pass. True OS-cold startup is **unmeasured**; this benchmark seeds a
snapshot and warms the executable, so it cannot establish that result.
Online-ready is **unavailable** while offline (reported as -1), not synonymous
with the cached shell or deferred-service milestone. Overall startup acceptance
and any required optimization remain part of the final performance gate #35.

All added translation units are C. Release executable import inspection shows
Windows/C runtime dependencies and no C++ standard-library import; no new
platform library is needed for this module. This does not assert whole-process
RAM, DLL residency or the remaining global performance targets.
