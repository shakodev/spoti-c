# Readable status and recovery messages (#40)

Windows 11 build 26200, MSVC 19.51, pure C17, `/W4 /WX`, 2026-10-20.
The [UI contract](../specs/status-messages.md) records the source findings and
bounded-storage behavior before implementation. No Spotify endpoint or protocol
was changed. The module is shared by account notices, shell errors, catalog and
playlist failures, cached-library recovery and lyrics availability messages.

## Final results

- [Isolated Debug/ASan](status-messages/isolated-asan-ctest.txt): **63/63 pass**,
  including all explicitly enabled local WARP and native-message checks.
- [Combined workspace Debug/ASan](status-messages/workspace-asan-ctest.txt):
  **59/59 pass**. This includes the pre-existing uncommitted work and its caption
  opt-in; those unrelated edits are preserved outside the issue commit. An
  [earlier run](status-messages/workspace-asan-first.txt) hit the existing caption
  maximize and window-device-count assertions. Both passed on
  [focused rerun](status-messages/workspace-asan-rerun.txt), followed by the clean
  full 59-test run above. No assertions were relaxed.
- [Full Release suite](status-messages/release-ctest.txt): **66/67 pass**. The
  unchanged 600-second `bench_headless` fails its strict 8,000,000-byte peak
  resident limit with **8,142,848 bytes**. Playback is healthy, with 13 seeks,
  zero underruns, balanced shutdown and normalized CPU 0.049260383%.
  [Raw samples](status-messages/headless-metrics.jsonl). No UI change enters
  this benchmark's source/link dependency closure.
- [Final Release message acceptance and audit](status-messages/release-message-acceptance.txt):
  both glyph-pixel and native-dialog tests pass. The existing dependency audit
  reports **376 pure-C translation units, six artifacts and zero failures**;
  [full audit](status-messages/release-audit.json). Initial worktree checkout
  converted yyjson line endings; the three files were restored to the exact
  manifest-verified committed bytes before this final audit. No dependency,
  manifest or import rule was changed.
- The Release GUI is **1,007,104 bytes**. Its exact SHA-256 and static resource
  identity are in the [REA receipt](../specs/status-messages-release-rea.json).

The separately opted-in #35 whole-client `bench_idle` remains a failure:
peak resident memory is **61,972,480 bytes idle / 66,236,416 paused**, above the
unchanged 20,000,000-byte limit. Both phases are quiet: one deliberately induced
diagnostic wake, no input contamination or additional rendering, and normalized
CPU **0.012893833% / 0.0%**. Shutdown is balanced.
[Raw record](status-messages/bench-idle.jsonl). These results preserve the existing
release blockers; this issue does not claim that the aggregate release gate passes.

## What is verified

The exact regression message is:

> Personalized Home is unavailable. Showing your playlists.

The notice's measurement and drawing keys bypass the shared 260-DIP ellipsis
policy. Its glyphs wrap to the actual pane width. Inline height is bounded and
**Read messages** remains in the fixed pane header, including at 400 x 320 DIPs.
The full-message dialog uses a read-only, selectable native multiline edit with
vertical scrolling, so unbroken tokens and longer recovery text remain readable.
Information, Warning and Error have visible labels and separate colors/backgrounds.

`app_messages` tests all of the following:

- The exact Home notice at 400/760/1100/1440 DIP widths and 96/120/144/192/384
  DPI. All non-space bytes reach untrimmed glyph runs inside the pane; narrow
  layout is taller than wide layout.
- Short, long, explicit multiline, combining-character, CJK, emoji and RTL UTF-8
  input; 1023-byte unbroken messages; invalid UTF-8, embedded NUL, negative lengths,
  oversized input and transcript exhaustion.
- Tab/Enter access to Read messages, exact retained snapshots, player/body
  separation, and 1000 settled layout/render frames with no CRT heap calls,
  no new text preparation and no pending font work.

The explicitly enabled `app_message_pixels` test uses the real DX11 WARP and
DirectWrite pipeline. Its rendered notice pixels must equal an independently
configured uncapped reference, including Arabic RTL and multiline CJK/combining
text. It also reproduces the former policy and verifies the old run is trimmed.
This is glyph/pixel evidence; fixture rectangle dimensions alone are not used as
proof of readability. The Home page uses the checked-in synthetic collection
fixture, with no account or network access.

The separately enabled `app_message_dialog` test exercises the production native
dialog and checks exact UTF-16 contents, read-only behavior, wrapping of a 400-byte
unbroken token, vertical scrolling, resize, Close bounds, Tab/Enter, Escape,
parent re-enabling and balanced GDI font cleanup. A synthetic WM_DPICHANGED checks
the font/geometry handler at 192 then 96 DPI; it is not a claim about a physical
192-DPI monitor. The native capture is at the desktop's actual 96 DPI.

## Captures

- [Before: reproduction of the former 260-DIP ellipsis policy](status-messages/message-before-reproduction.png).
  This reproduces the old glyph policy in the same fixture shell; it is not a
  screenshot of a historical executable or signed-in Spotify account.
- [Default width after the fix](status-messages/message-after-default.png).
- [400-DIP narrow pane, full fallback message](status-messages/message-after-narrow.png).
- [Wide pane at 192 DPI](status-messages/message-after-wide-192.png).
- [Multiline UTF-8 at 120 DPI](status-messages/message-multiline-120.png).
- [RTL text at 144 DPI](status-messages/message-rtl-144.png).
- [Native scrollable details](status-messages/message-details-native.png).

Captures are lossless PNG conversions of renderer RGBA/BMP readback or actual
desktop pixels. `tools/message_captures.py` performs format conversion without
rescaling or changing the captured image.

## Reproduction

Use an x64 Visual Studio developer shell and the pinned Python environment:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Explicit local GPU and visible native-dialog opt-ins; no account/device use.
cmake -B build -DENABLE_RENDERER_GPU_TESTS=ON -DENABLE_MESSAGE_NATIVE_TEST=ON
cmake --build build
ctest --test-dir build --output-on-failure

cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF `
  -DENABLE_RENDERER_GPU_TESTS=ON -DENABLE_MESSAGE_NATIVE_TEST=ON
cmake --build build-rel
ctest --test-dir build-rel --output-on-failure
python tools/message_captures.py build-rel docs/verification/status-messages
```

The existing #35 gate sources, thresholds, memory counters and timing semantics
are unchanged. Static UI/SMTC state remains under its existing 256 KiB assertion;
the scalar word-measure cache retains shorter strings to make room for bounded
message storage. The glyph atlas and raster scratch budgets are unchanged.
No account, physical audio device, cold-start or whole-client release-gate
acceptance is implied by these UI tests. The previously documented aggregate
release blockers remain separate from the message-readability acceptance.
