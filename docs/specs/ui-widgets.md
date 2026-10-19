# Native widget and virtual-list contract (#29)

Evidence recorded before implementation, 2026-10-19. REA MCP
`analyze_javascript_application` selected `xpui-routes-playlist.js` and
`xpui-routes-search.js` in the extracted `ref/xpui` directory. Parent Evidence:
`ev_fe64916f0d47984b45cc61cb80581fa147353b30997f5b5ac5075ee3f3e89a4a`;
summary: `ev_61e51fe32f4126721ecd9c5cdeac002c35dea2f019a1006161a0fe726f786dff`.
Directory SHA-256: `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Two files parsed, 151720 text bytes, zero parse failures. The remaining 269
files were not analyzed; semantic and application coverage are partial. Static
analysis did not execute Spotify or observe network traffic.

REA module view `ev_c2e50e58db2e4ba12949a8d4a2ec9b5bcacd0f63ab397ed67c78f5ed7b5cc678`
records playlist's unresolved shared import `36839` at line 1 columns
10254–10262, artifact SHA-256
`ba01d3c8d15eda04889d5087274b7ee7f07511fd0f447897e3a5ab9e6671e9f5`.
Supplemental literal inspection of the selected playlist source observes a
tracklist consumer passing `renderItem`, `tracklistRef`, and `extraRowHeight`,
and a searchbox consumer passing `onChange`, `onKeyDown`, `onFocus`, `value`,
`maxLength:80`, and a 200 ms debounced change callback. These consumer strings
do not establish the shared implementation, virtualization overscan, stable-key
strategy, clipboard/IME semantics, or seek submission behavior. Search's
selected route does not resolve those missing widget implementations either.

The following are native design decisions required by issue #29, not inferred
Spotify contracts. No HTTP endpoint, header, or query hash is added.

- UI-thread-owned widget context: at most 256 KiB of state, a single persistent
  Clay arena plus frame scratch totaling at most 1.25 MiB. Explicit sticky full
  errors and an error callback; failed layouts emit a visible error panel.
- Buttons use stable caller-supplied identities and pointer capture; focus is
  retained independently of materialization. No persistent per-row storage.
- Fixed-height virtual lists compute a half-open interval in double precision,
  clamp overscan to 16 rows per edge and reject a viewport needing over 256
  materialized rows. Pixel coordinates are logical DIPs. Layout rebases visible
  rows near the viewport to avoid huge float coordinates. Logical spacers remain
  double precision. Callers supply unique stable item keys and an O(1) lookup
  for focus navigation across sorting/filtering. Selection and focus survive
  culling; removing an item requires an explicit model reset.
- Text editing uses a bounded UTF-8 buffer, scalar-boundary selection, strict
  validation, transactional replacement, and a separate bounded composition
  preview. Clipboard adapters run only at input-event boundaries; a Win32
  adapter converts CF_UNICODETEXT. IME adapters provide UTF-16 preedit/result
  strings; canceled preedit does not alter committed text. Scalar navigation
  is supported; grapheme-cluster and bidi visual navigation belong to a later
  shaping-aware editor.
- Seek dragging retains the latest preview, consumed once by the caller;
  release/keyboard commit yields a separate committed seek. Cancel restores
  the original value. No polling clock is needed for drag or hover. A focused
  text caret requests only its next blink deadline; blur/composition disarms it.
- All layout/measurement/render paths use caller memory. OS clipboard/IME
  acquisition is outside layout. Allocation-hook tests cover repeated frames;
  Release-only benchmarks compare equal visible-row counts across total sizes.

Public headers document ownership and event boundaries. The primitives are
reusable building blocks; application screens own routing Win32 input events
and draining committed actions into the playback/network command queues.

## Integration and ownership

Allocate `UiWidgets`, one 64-byte-aligned `UI_CLAY_BYTES` block and one
`UI_SCRATCH_BYTES` block once. `ui_widgets_init` installs a simple bounded
fallback measurement function. Production screens bind the existing prepared
DirectWrite cache with `font_bind_clay` after initialization. Prepare font
misses before layout, as specified by the font module. Dispose the widget
context before releasing its buffers; no widget teardown allocates or frees.

Assign the eight text, 32 seek and eight list slots in `UiWidgets` to stable
screen controls. Buttons use the context's single focus/capture IDs without
allocating persistent state. Call `ui_widgets_focus` from the screen's keyboard
focus order, and `ui_virtual_navigate` with the model's key/index mapping for
list navigation. Route wheel deltas to the list's scroll field and scrollbar
drag events through the layout input. Pointer coordinates and viewport sizes
are DIPs (divide native pixels by DPI/96 once at the input boundary).

Between input events, screens call `ui_widgets_begin`, declare controls and
visible list rows, then call `ui_widgets_end` and pass its commands to
`ui_frame_clay`. Render commands borrow the persistent Clay/text buffers until
the next layout or text mutation. Row callbacks must obey the same allocation
and lifetime rules. Scratch is valid only until end and never backs persistent
text. The context rejects more than 400 widget declarations, and Clay separately
enforces its 512-element/1024-word capacities. Oversized viewport work returns
`UI_WIDGET_FULL`; callers must reduce viewport density or show the reported
error, rather than silently omitting visible rows.

Route UTF-8 text entry to `ui_text_replace`, editing/selection keys to
`ui_text_key`/`ui_text_select`, and copy/cut/paste to `ui_text_clipboard` using
`ui_clipboard_win32(hwnd)` (or a deterministic host adapter). Feed
`WM_IME_COMPOSITION` flags to `ui_text_ime_message`; a message can commit a result
and start a new composition transactionally. `WM_IME_ENDCOMPOSITION` cancels
remaining preedit. Suppress duplicated committed `WM_CHAR` output when using
this explicit result-string path. Native IME candidate placement and hit testing
of shaped text belong to the host screen. Selection offsets are UTF-8 scalar
boundaries, not UTF-16 units; the IME adapter converts and checks both.

Keep button hover and slider updates event driven. Drain at most the latest
`ui_seek_take_preview` for display, and send `ui_seek_take_commit` to the existing
playback command queue. Keyboard slider controls call `ui_seek_key` with a new
normalized value (Home=0, End=1). These primitives never call playback or perform
network requests themselves. `ui_text_tick` returns the caret's next 500 ms
deadline, or `UI_CARET_NEVER` when blurred/composing; hosts arm a one-shot wait
only for an active caret and dirty the UI when visibility changes. No widget
uses a background thread, recurring hover timer, or perpetual animation clock.

## Verification

The x64 MSVC Debug build uses `/W4 /WX /fsanitize=address`. The full default
`ctest --test-dir build --output-on-failure` suite passes (47 tests). The two
new targets are `ui_widgets` and `ui_virtual_list`. Debug CRT allocation hooks
observe zero calls over 2000 widget layout/measurement/renderer frames and 3000
list scroll/hover frames. State capacity and scratch/Clay budgets have compile
time assertions (x64 widget state: 22432 bytes; configured Clay minimum: 441024
bytes; scratch: 65536 bytes), and runtime exhaustion produces an error callback plus a red
error panel. The module owns no persistent OS handles. Clipboard ownership
transfers to Windows only after successful `SetClipboardData`; failure frees
the temporary handle, and IME context acquisition always has a matching release.

Tests cover 50000 randomized range invariants, empty/one/10000/UINT32_MAX rows,
DBL_MAX scroll, fractional row heights, capped overscan, scrollbar endpoints,
offscreen selection/focus, reordered model navigation, stable row IDs across
different materialized slots, clipping, malformed UTF-8/surrogates, reversed
selection, transactional paste/composition overflow, IME cancel/commit, caret
deadlines, slider pointer capture, endpoint clamp and preview/commit coalescing.
Clipboard behavior uses a deterministic fake; native UTF-16 conversion is tested
without requiring a live clipboard or installed interactive IME.

Release benchmark command: configure `build-rel` with
`-DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF`, build `test_virtual_list`, then
`ctest --test-dir build-rel -R '^bench_ui_virtual_list$' -V`. The benchmark is
registered only in that configuration. The final local run measured 14.780
microseconds/layout for 10000 rows and 14.716 for one billion rows; both emitted
280000 rows over 10000 layouts. Timing is observational, not a hardware-specific
pass threshold; deterministic checks enforce the same bounded work count.
