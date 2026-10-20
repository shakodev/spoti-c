# Caption verification (#37)

Windows 11 build 26200, MSVC 19.51, C17, `/W4 /WX`, 2026-10-20.
The [diagnosis and ownership contract](../specs/window-caption.md) was recorded
before the frame change. The fix restores native nonclient caption ownership;
no Spotify endpoints or artifacts were inspected or changed.

## Automated checks

- Working tree: all 58 Debug/ASan tests passed, including the explicitly enabled
  native caption capture test. No compiler warnings or ASan findings.
- Isolated staged snapshot against the committed baseline (excluding unrelated
  local edits): all 54 Debug/ASan tests passed. The final capture/activation
  refinements were also rerun against this snapshot.
- Release: all 11 selected checks passed: caption capture, window, scheduler,
  renderer HWND/WARP, font/artwork WARP, UI/DX11 idle, release measurement
  semantics and dependency audit. The audit reports only C translation units,
  no forbidden runtime dependencies, and an executable below 2 MB.
  The isolated snapshot also passes these 11 checks. Its executable is 989,184
  bytes, with identity retained by [REA](../specs/window-caption-release-rea.json)
  and the [dependency audit](window-caption/release-audit.json).
- Native geometry covers 96/120/144/192 DPI notifications, suggested rectangles,
  narrow/default/wide widths, active/inactive and normal/maximized states. It
  requires all three accessible button rectangles to be distinct and above the
  client rectangle. Hit testing delegates to Windows instead of guessed button
  widths. Resize edges, system-menu availability, minimize/restore, double-click
  maximize/restore and Alt+F4/orderly close pass.
- The opt-in desktop harness injects real pointer clicks into OS-reported button
  rectangles. It verifies minimize, maximize, restore and close state changes,
  and captures hover/pressed appearance. Close leaves the HWND alive for the
  owner to destroy after resource cleanup. The real application header and
  Search control are rendered below the nonclient caption.

The default tests use offscreen HWNDs; the capture harness uses a visible
topmost HWND with WARP DX11 and actual composed-desktop pixels. This is an
explicit desktop/GPU test opt-in. No account or physical audio-device tests were
enabled. Synthetic DPI notifications do **not** establish visual monitor-DPI
acceptance. Interactive shell snap flyouts and physical monitor transitions
remain unobserved; their native message handling is retained.

## Visual matrix

Screenshots below are lossless PNG crops of the top 160 physical pixels of
composed Release HWND captures, without resizing or recoloring. Their filenames
include the actual `GetDpiForWindow` value. Full BMP captures remain in the
local build's `caption-captures/` directory. The before capture uses the
unchanged frame and the existing surface placeholder; after captures use the
real app layout in its signed-out state.

| Actual display scale | Narrow/default/wide, normal and maximized | States |
| --- | --- | --- |
| 100% / 96 DPI | Captured and inspected | Active/inactive, hover/pressed, maximize/restore |
| 125% / 120 DPI | Pending actual desktop configuration | Pending |
| 150% / 144 DPI | Pending actual desktop configuration | Pending |
| 200% / 192 DPI | Pending actual desktop configuration | Pending |

- [Before](window-caption/100/before-default-96.png).
- [Narrow](window-caption/100/narrow-96.png), [default](window-caption/100/default-96.png),
  [wide](window-caption/100/wide-96.png).
- Maximized from [narrow](window-caption/100/narrow-maximized-96.png),
  [default](window-caption/100/default-maximized-96.png),
  [wide](window-caption/100/wide-maximized-96.png).
- [Inactive](window-caption/100/inactive-96.png).
- Hover/pressed: [minimize hover](window-caption/100/minimize-hover-96.png),
  [minimize pressed](window-caption/100/minimize-pressed-96.png),
  [maximize hover](window-caption/100/maximize-hover-96.png),
  [maximize pressed](window-caption/100/maximize-pressed-96.png),
  [restore hover](window-caption/100/restore-hover-96.png),
  [restore pressed](window-caption/100/restore-pressed-96.png),
  [close hover](window-caption/100/close-hover-96.png),
  [close pressed](window-caption/100/close-pressed-96.png).

## Reproduction

From an x64 Visual Studio developer shell:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_CAPTION_VISUAL_TEST=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DENABLE_CAPTION_VISUAL_TEST=ON -DENABLE_RENDERER_GPU_TESTS=ON
cmake --build build-rel
ctest --test-dir build-rel -R '^(ui_caption_visual|ui_window|ui_scheduler|ui_renderer_window|ui_renderer_warp|ui_font_warp|ui_artwork_warp|bench_ui_idle|bench_ui_dx11|core_release_metrics)$' --output-on-failure
```

Repeat `ui_caption_visual` after setting each actual display scale. Archive each
run before changing scale. Requested normal widths are 420/1100/1440 DIPs,
clamped to the monitor work area, with actual physical sizes printed by the
harness. A sufficiently large display is needed for full-width high-DPI cases.
The harness restores the cursor and destroys its test windows on success.

## Existing #35 performance gate

The whole-process `bench_idle` gate was run unchanged. Both idle and paused
phases reported zero normalized CPU, no spontaneous wakes/timeouts/presents,
and balanced shutdown, but failed the existing strict 20 MB resident limit.
Working-tree resident memory was 61,788,160 bytes at the first idle sample.
The prior checked-in [record](release-gate/bench-idle.jsonl) already fails that
limit (57,344,000 bytes at its first idle sample), and its
[CTest log](release-gate/release-ctest.txt) records both Release gate failures.
This change does not weaken the gate or claim final performance acceptance.
The isolated issue-only Release [idle trace](window-caption/bench-idle.jsonl)
also fails the resident limit: 62,025,728 bytes at its first idle sample, with
0.012923461% normalized CPU for idle and 0% for paused (both below the unchanged
0.05% CPU threshold). Both phases are quiet and shutdown is balanced. Historical
samples are not a controlled before/after memory comparison.
