# Catalog / Lyrics screen verification (#32)

Verified on Windows x64, 2026-10-19, MSVC C17, Ninja, `/W4 /WX` and ASan.
The [spec and retained REA evidence](../specs/catalog-screens.md) were recorded
before implementation. Existing #23/#24 APIs supply all request and parser logic;
no new Spotify endpoint or guessed hash is introduced.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

All **50/50 default ASan tests pass**, with zero compiler warnings or sanitizer
errors. `app_catalog_screens` is the new deterministic target; no account,
physical-device, benchmark or GPU opt-in test is required by default CTest.
The optimized Release app and catalog test also build cleanly, and the targeted
Release `app_catalog_screens` test passes.

The catalog tests cover:

- Real fixture-worker navigation Search -> Artist top tracks -> releases ->
  Album -> existing PLAYER_LOAD -> Lyrics -> back, restoring query/section.
- One 250 ms debounce deadline, rapid replacements, cancellation, stale owned
  page/document deliveries and explicit transport-full/error outcomes.
- Stable compact IDs and occurrence keys; search and album continuation replace
  the current page. Artist continuation remains unavailable through the API.
- Empty, partial, boundary, malformed, unavailable/restricted, authentication,
  rate-limit and offline states; long names truncate at UTF-8 scalar boundaries.
- Bounded native UTF-16/surrogate editing, Backspace and shared IME/clipboard
  handling. Tab/Shift-Tab, Enter and Home/End navigation at 96/120/144 DPI.
- Actual audible-position input, backward/forward seek, wrong-track retirement,
  plain/capped/missing lyrics and a five-second wheel/keyboard/scrollbar hold.
  A wall-clock jump alone cannot change the highlighted line.
- A fake two-second playing interval emits exactly two progress and four lyric
  timer invalidations, with zero animation ticks. Hidden/paused lyrics disarm.
- 600 asynchronous route transitions, 1000 warm layouts with zero tracked CRT
  heap calls, and zero shared page reservations after leaving the catalog.
  Repeated app/worker lifetimes restore process handle counts to baseline.

The existing `audio_player` test now requests 100 clock samples from another
thread, coalesces them without consuming playback-command slots and verifies
the acknowledged position came from the fake sink, with unchanged playback
generation. `audio_headless_smoke` verifies the real decode owner acknowledges
an explicit refresh. UI timer wakes request that refresh rather than deriving
position from wall-clock time. Acknowledgements can cause a bounded fresh-state
repaint; audio callback periods do not wake the UI.

The shared page pool stays 1,040,416 bytes in its existing 1 MiB reservation.
App, widgets and catalog/lyrics control buffers total **251,328 bytes**, below
the 256 KiB UI-state reservation. The offline host's model/control overhead is
**519,104 bytes**, below 512 KiB. There is no extra JSON pool, growing result
list or heap-backed lyric text copy. Explicit page-pool disposal refuses live
readers and restores quiescent caller storage for reuse under ASan. These fixed
bounds do not measure whole-process RAM, executable size or cold startup.

## Visual review and use

`build/test_catalog_screens.exe --visual` passed under ASan using real
DirectWrite and offscreen DX11/WARP. It exports all four screens at 500x800 /
96 DPI, 1375x900 / 120 DPI and 1800x1125 / 144 DPI. The losslessly converted
captures were inspected for title/subtitle clipping, durations, unavailable
rows, lyric highlighting and playback-bar separation:

| Screen | 100% | 125% | 150% |
|---|---|---|---|
| Search | [capture](catalog-search-96.png) | [capture](catalog-search-120.png) | [capture](catalog-search-144.png) |
| Artist | [capture](catalog-artist-96.png) | [capture](catalog-artist-120.png) | [capture](catalog-artist-144.png) |
| Album | [capture](catalog-album-96.png) | [capture](catalog-album-120.png) | [capture](catalog-album-144.png) |
| Lyrics | [capture](catalog-lyrics-96.png) | [capture](catalog-lyrics-120.png) | [capture](catalog-lyrics-144.png) |

The optional CTest `app_catalog_visual` is registered only with
`ENABLE_RENDERER_GPU_TESTS=ON`. Its renderer resource/texture and font COM-object
counters return to zero. GPU/DirectWrite process-global caches are outside the
deterministic app/worker handle baseline; this visual run is not a process-wide
OS leak detector.

Run `build/spotic.exe --demo`, choose Search and enter a query. Open the
synthetic artist, choose Albums, open the album and play its first song. Lyrics
opens from the playback bar; wheel scrolling temporarily stops following, and
Resume follow restores it. The demo host is explicitly offline and uses the
existing synthetic audio fixture. Default startup remains signed out without a
live catalog/account transport. Missing track artwork/identity produces an
explicit lyrics-unavailable state. Account/device checks were not run; these
captures and fixtures do not establish current Spotify compatibility.
