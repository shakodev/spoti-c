# Playlist / Tracklist verification (#31)

Verified on Windows x64, 2026-10-19, MSVC C17, Ninja, `/W4 /WX`. The
[REA evidence and native design](../specs/playlist-screen.md) were recorded
before implementation; there are no new Spotify endpoints.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

All 49 default ASan tests pass with zero compiler warnings or sanitizer errors.
The targeted Release shell/playlist tests also pass. `app_playlist` uses a generated immutable
10,000-record disk fixture, not a giant checked-in JSON page. Tests cover:

- Widths/DPI, virtual scrolling across metadata batches, bounded Clay row count,
  stable selection and keyboard context, real player transfer/play/pause.
- Duplicate song IDs with distinct ordinals, unavailable tracks, missing
  metadata and explicit full-index/loaded-only/window coverage.
- ASCII case folding, exact non-ASCII matching, invalid UTF-8/query overflow,
  source/title/duration ordering, stable equal-title ties and cached sorting.
- Running/stale jobs, revision changes invalidating selection, source failures
  without automatic retries, and the 10,005-record disk-window boundary.
- Repeated route/back navigation with fixed model storage, cancellation before
  teardown and balanced application-owned handles. The visible metadata copy
  remains usable after its source file and workers have closed.
- 2000 warm layouts with zero tracked CRT heap calls, followed by 1000 real
  DirectWrite/Clay/CPU-render frames with zero heap calls, pending text or new
  shaping. This is application allocation tracking, not a process-wide OS leak
  detector. DirectWrite owns additional process-global caches.

The existing `app_shell` test also selects a playlist through the real offline
host, receives its generated 10k index and checks window coverage and teardown.
The shared playlist/filter/order/metadata plus collection model size is 511416
bytes; App plus widgets is 148248 bytes. Compile-time checks enforce their
512 KiB and 256 KiB reservations, including offline host overhead in its check.
Process-wide RAM/startup/executable-size acceptance remains a later gate.

## Release measurements

```powershell
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF
cmake --build build-rel
ctest --test-dir build-rel -R "^(bench_tracklist)$" --output-on-failure -V
```

The benchmark is registered only for Release/non-ASan. The reference host
reports AMD64 Family 23 Model 8 Stepping 2; these are local measurements, not
cross-machine guarantees. Forty filter samples use 10,000 immutable 264-byte
records in a warm OS file cache. They include bounded reads, UTF-8 validation,
matching, compact IDs and result-map construction; layout uses the real Clay
screen with prepared visible metadata and fallback scalar text measurement.

| Measurement | Result |
|---|---:|
| Source-order filter median / p95 | 1.867 / 1.929 ms |
| Cached-title filter median / p95 | 1.824 / 1.952 ms |
| Clay layout, 10,000-frame mean | 0.064 ms |
| Initial title-order build, worker | 20.345 ms |
| Declared virtual rows in that layout | 11 |

Both filtering p95s and mean layout pass the <=16 ms gate. Initial sorting is
asynchronous and separately reported; subsequent queries reuse its order.
Cold disk latency, longer titles and live transport can cost more. This does
not measure full GPU presentation, request latency or whole-client startup.

## Visual QA and use

`test_playlist_screen --visual` exports real DX11/WARP frames under ASan at
125% DPI after font preparation, with no pending text. Its CTest entry is
present only with `ENABLE_RENDERER_GPU_TESTS=ON`. The losslessly converted
[narrow](playlist-narrow.png), [default](playlist-default.png) and
[wide](playlist-wide.png) captures were inspected for columns, clipping,
unavailable rows and playback-bar separation. They use synthetic data.

Run `build/spotic.exe --demo`, select a playlist from Home or Library, type
in Filter, and cycle Order/Title/Time. Tab/Enter, arrows, Home/End and Page
Up/Down provide keyboard access; Back returns to the previous route. Larger
lists expose Prev 10k / Next 10k and clearly label window-only results.
The queue is a finite copied 64-ID transfer with revision provenance; `more`
indicates a boundary, not background full-playlist auto-refill.

The standalone default is still signed out without live collection/account
transport. Production hosts supply immutable normalized records from the
existing verified contracts. Account and physical-device tests were not run;
this offline verification does not establish current Spotify compatibility.
