# Shared visual foundations — issue #41

Date: 2026-10-20. Work is isolated on `feat/issue-41`, based on
`92a2fbbbb278656f8b3d46326f1e2a6160b16956`.
**Issue #41 is not yet visually accepted.** Reference screenshots and
100/125/150/200% side-by-side comparisons remain outstanding.

The [specification](../specs/spotify-desktop-parity.md) was written before the
UI modules. It identifies installed Spotify 1.3.5.3058 separately from the
historical 1.3.4.258 extraction. Checked-in REA receipts cover PE resources and
selected packaged JavaScript; the CSS measurements are supplemental static
evidence. None establishes the active account's UI variants.

## Implemented and tested scope

The shared theme, procedural icons, button/card/row/tab/status primitives,
keyboard menu, focus navigation and artwork cover crop use bounded storage.
Semantic font roles share measurement/rendering cache keys; DirectWrite
provides shaping/fallback. Windows reduced-motion changes cancel animation
deadlines without adding an idle timer. Existing caption styling, message
handling, volume commands and persistence are preserved.

Automated coverage includes all six control states, pointer capture/cancel,
disabled activation, focus traversal and RTL, menu dismissal/focus restoration,
oversized-label clipping, artwork UV/corner validation, renderer dispatch tags,
capacity errors, and repeated allocation-checked warm frames. The existing
font, scheduler, window, shell and volume unit coverage remains enabled.
These tests do not establish live account, visible desktop or GPU acceptance.

The fresh worktree exposed missing LF rules for yyjson's three hashed upstream
files. `.gitattributes` now preserves their existing manifest bytes across
Windows checkouts; vendor content and hashes are unchanged. The native window
test also now checks exactly one additional callback for its synchronous
device message, allowing ambient notifications before the assertion.
The clipping test checks actual renderer scissors using oversized ink.

## Verification results

MSVC 19.51.36252, x64 C17, Windows SDK 10.0.26100.0, Ninja:

| Check | Result |
| --- | --- |
| Debug build, `/W4 /WX /fsanitize=address` | passed, zero warnings |
| Full default `ctest --test-dir build --output-on-failure` | **56/56 passed**, 58.53 s |
| Release build, `/W4 /WX`, ASan off | passed, including optional capture binaries |
| Selected Release correctness + dependency audit | **14/14 passed**, 3.62 s |
| Dependency/source/import audit | 382 C translation units, 6 artifacts, zero failures |
| GPU visual, account and reference comparisons | **not executed / pending opt-in** |

Raw results: [ASan](spotify-desktop-parity/asan-ctest.txt),
[Release](spotify-desktop-parity/release-ctest.txt),
[source/license/import audit](spotify-desktop-parity/release-audit.json).
The audit finds no C++ runtime, ASan runtime or runtime shader compiler in
the six executables' direct imports, verifies pinned dependency licenses and
hashes, and preserves the existing executable-size thresholds.

The Release GUI is **1,020,416 bytes**, SHA-256
`6aea9c8ad4869f99ac5e2a70aa8a76958b36cd7e4e29d18bea384a5315452a2d`.
REA PE resource receipt
`ev_5116e082425e9add40bbdcc2349ce4b7f3c257221156a55d4cde3d6eaa5b48d2`
is [retained with the specification](../specs/spotify-desktop-parity/native-release-pe.json).
This is a static binary identity/resource observation, not visible GUI acceptance.
The headless executable is 528,384 bytes; all audited artifact hashes are in
the audit record.

The Debug cache keeps all optional GPU/live/native acceptance flags OFF.
The Release cache enables `ENABLE_RENDERER_GPU_TESTS`,
`ENABLE_PARITY_VISUAL_TESTS` and `ENABLE_RELEASE_NATIVE_BENCH` to compile their
targets, but executes only the 14 tests named in the Release log.
The native performance workloads were not run and the #35 gate is not claimed
to pass.

## Reproduction

Use the x64 Visual Studio developer shell and Python with the repository's
pinned protobuf tooling. The local run pins
`C:/Users/Daan/Documents/Projects/spoti-c/.cache/proto-tools/Scripts/python.exe`.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional GPU targets are separately enabled. **Run them only after explicit
opt-in.** Compilation alone is not acceptance:

```powershell
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DENABLE_RENDERER_GPU_TESTS=ON -DENABLE_PARITY_VISUAL_TESTS=ON
cmake --build build-rel
ctest --test-dir build-rel --output-on-failure -R "^(ui_components_visual|ui_renderer_warp|ui_font_warp)$"
```

The component harness produces eight native DX11 WARP captures: LTR/RTL at
96/120/144/192 DPI, each rendered independently at a 1040x1000 DIP viewport.
It checks warm font-cache settling, heap hooks and resource teardown.
The renderer's optional pixel tests cover icon geometry/mirroring/clipping and
rounded artwork cover crop; the font test covers all four scales.
WARP pixels are controlled native fixtures, not physical GPU or Spotify
reference evidence.

Export one raw capture without resampling:

```powershell
python tools/parity_compare.py build-rel/parity-captures/showcase-120-ltr.rgba --metadata build-rel/parity-captures/showcase-120-ltr.json --output .cache/parity/125
```

For an actual reference comparison, crop the corresponding native and Spotify
components to the same physical size at the same DPI. Record crop origin,
component/state, original screenshot hash and all reference metadata below.
Keep the original images. Pass `--reference reference.png` and
`--reference-metadata reference.json` to the same command. The tool rejects
mismatched pixel size/DPI, writes reference/native/side-by-side/difference PNGs,
and reports pixel hashes and absolute RGB error. A numerical score alone
cannot certify wrapping, shaping, interaction or service behavior.

## Reference capture protocol and open acceptance

The Windows Computer Use helper reported:
`Computer Use native pipe is unavailable: failed to connect native pipe:
The system cannot find the file specified. (os error 2)`.
The explicit GPU/account inspection question is unanswered. No Spotify window
was captured and no optional GPU test was executed during this work.

After opt-in and restoration of a capture route, use a dark-theme reference at
each OS scale with in-app zoom fixed at 100%. Record UTC timestamp, EXE/SPA
hashes, app version, physical client rectangle, logical viewport, DPI, zoom,
language, theme, account/service availability and visible layout variants.
Capture only the app client; account details must be redacted before committing.
Record every state unavailable to that account/service explicitly.

| Required capture set | State | Current evidence |
| --- | --- | --- |
| Shell / Home | loaded, empty/error, focus/search | pending runtime capture |
| Library | expanded/collapsed, list/grid, selected/hover | pending runtime capture |
| Playlist / album / artist | artwork, headings, rows, context menu | pending runtime capture |
| Player | stopped/playing/paused, mute, compact, queue | pending opted-in playback/reference observation |
| Menu / tooltip | pointer, keyboard, focus restore, placement/timing | unit-tested native menu; reference pending |
| Shared controls | six states, LTR/RTL at all four scales | CPU geometry tests; GPU pixels/comparisons pending |
| Responsive layout | widths immediately before/after each transition | pending runtime measurement |

Capture shell content at a repeatable logical viewport (proposed 1280x800 DIP,
adjusted only if the monitor cannot contain it and recorded as a variant).
Measure breakpoints with DPI and zoom fixed. Preserve the existing native
compact threshold until actual observations justify changing it. Review
geometry and flat-color pixels separately from font rasterization; Segoe UI
fallback is a known metrics difference from proprietary SpotifyMix faces.
Original procedural icons also require silhouette comparison.

The #35 release performance thresholds and harness are unchanged. Existing
aggregate RAM/startup/CPU failures are not waived by these correctness tests.
GPU/account/device/cold-start acceptance remains separate.
