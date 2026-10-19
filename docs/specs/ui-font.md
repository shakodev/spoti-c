# DirectWrite shaping and bounded font cache (#27)

Recorded before implementation, 2026-10-19. This module uses installed system
fonts; Spotify font files are neither copied nor required. It adds no endpoint.

REA MCP `analyze_javascript_application` inspected the exact selected
`pip-mini-player-snapshot.js` in the pinned XPUI directory SHA-256
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Parent evidence `ev_281f5d89d1afb5eb927c32c6e9472e48c7e52f8fd1e896624e457751c0ae5c06`;
summary `ev_237f5ec9556204d16e09fc299c7fdeb54d2a55a4fb0daf310017df3d6af14598`.
One file parsed without failure; 270 inventoried sources were outside selection
and the semantic graph reached its per-file ceiling. This does not establish
Spotify's shaping, fallback, rasterization or atlas implementation.

Supplemental CSS reference inspection: `xpui-snapshot.css`, SHA-256
`fdc9112f7a179e06889ddf3f1f2e9d2c3f2f3860339b17b6b4bae03affcebea5`, defines
`.ellipsis-one-line` with `text-overflow:ellipsis`, `white-space:nowrap` and
`overflow:hidden`. This establishes presentation intent only. The cache limits,
DirectWrite pipeline and monochrome emoji policy below are local design choices.

## Implementation contract

Pure C COM bindings follow the installed Windows SDK's base DirectWrite ABI.
Prepare UTF-8 text by strict bounded conversion to UTF-16; malformed/truncated
input fails without accepting a prefix or splitting a surrogate. DirectWrite
TextLayout performs shaping, ligatures, bidi, combining marks and system font
fallback. Its custom text renderer captures resolved glyph runs and rasterizes
coverage during preparation, not during measurement or render submission.
Standard/contextual/required OpenType ligature features (`liga`, `clig`, `rlig`)
are explicitly enabled; glyph substitution still depends on the installed font.
Color emoji use their system font's monochrome outline; missing outlines retain
the system missing-glyph representation where available. No proprietary fonts or
color layers. A wholly unavailable outline returns an explicit backend error or
empty ink with the native advance; no custom emoji assets are synthesized.

The supported system families are Segoe UI and Consolas. Cache keys include
system face ID, size, weight/style, DPI, letter spacing,
line height, width/ellipsis mode and caller text generation plus exact UTF-8
bytes. Prepared text owns fixed bounded glyph placements and metrics. A miss
copies one bounded request to a fixed pending queue and emits one wake callback;
duplicate misses coalesce. The owner processes preparation and atlas uploads at
a safe boundary before Clay layout. Cached measurement and glyph submission do
not call DirectWrite, create COM objects, allocate, upload, or shape.

Use a 1024x1024 single-channel R8 GPU atlas (1 MiB) and a bounded LRU entry table.
Each atlas entry has a generation; cached glyph references validate it before
use. Eviction and DPI/device reset invalidate stale references explicitly.
Atlas entries cache rasterized visual glyph runs, retaining their shaped
placement and native advances; they do not deduplicate individual characters.
This preserves context-dependent shaping and fallback order without retaining
DirectWrite layout/face objects between preparations. The CPU atlas backing is
1 MiB, separate from the 256 KiB coverage scratch. There are 32 text runs (2048
UTF-8 bytes and up to 64 visual patches each), 16 pending requests and 128 atlas
entries packed into 32-pixel tiles with padding. Packing, request/run/glyph limits and long
text produce explicit full/invalid/error results rather than reallocations.
Atlas uploads happen only in the resource preparation phase. Rendering a stale
run queues preparation, never samples stale UVs. Retained run handles include a
generation and cannot silently address a reused slot.

Failures are negatively cached until key generation changes, reset or LRU
replacement, preventing oversized/unsupported text from creating a retry wake
loop. Successful preparation and DPI reset clear Clay's own measurement cache,
so it cannot retain temporary miss metrics. Destruction unbinds the measurement
user data while the borrowed Clay context is still alive. Unchanged atlas epochs
perform no upload; new coverage updates the existing R8 texture at preparation
boundaries. Device recovery reuploads the CPU atlas into a new texture handle.

Ellipsis is shaped by DirectWrite's inline trimming object, so truncation does
not split shaping clusters. Layout metrics include trailing whitespace and are
kept in logical pixels; glyph placements use physical raster bounds divided by
DPI scale. Clay measurement uses cached metrics. The next Clay layout pass can
consume newly prepared misses after one invalidation; no idle polling timer.

The application surface prepares its system-font caption in the window's
resource phase and submits cached glyphs through DX11. The pinned Clay v0.14
implementation is compiled in one C translation unit. Its existing MSVC numeric
conversion/unused-variable diagnostics are scoped off only around that unchanged
upstream include; project modules retain `/W4 /WX`. The C COM declarations are
checked against Windows SDK 10.0.26100.0's `dwrite.h`/`dwrite_1.h`, with static
assertions for called vtable offsets. No C++ translation unit is introduced.

Platform references:
[custom text rendering](https://learn.microsoft.com/en-us/windows/win32/directwrite/rendering-directwrite),
[DrawGlyphRun](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextrenderer-drawglyphrun),
[glyph alpha textures](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwriteglyphrunanalysis-createalphatexture).
OpenType feature application follows
[DirectWrite typography](https://learn.microsoft.com/en-us/windows/win32/directwrite/glyphs-and-glyph-runs).

## Verification (Windows x64 / MSVC, 2026-10-19)

- All 44 default CTest tests pass with Debug ASan and optional GPU tests OFF.
  Project modules compile with `/W4 /WX`; the pinned Clay implementation uses
  only the scoped compatibility diagnostics described above.
- `ui_font` checks strict UTF-8/surrogates, empty strings, composed/combining
  accents, Arabic contextual advances and bidi, ligature input, CJK/emoji
  fallback, missing glyphs, spacing, line height, face/DPI/text-generation keys,
  a 1900-character ellipsis case and explicit scratch overflow. Observed Segoe
  UI 16-DIP advances: Hello 36.852, Arabic fixture 44.086, emoji 21.969; the
  80-DIP trimmed fixture is 75.656. Native font revisions can change glyph slots,
  so contextual-advance checks do not assume one glyph per joined form.
- Both direct cached calls and 1000 actual warm Clay layout/render frames pass
  the Debug CRT allocation hook with zero calls. Misses coalesce into one wake;
  failed preparation does not requeue. Atlas LRU eviction invalidates UVs while
  a text-run slot is still owned. Reset and reuse invalidate old run handles.
- Opt-in ASan `ui_font_warp`, `ui_renderer_warp` and `ui_renderer_window` passed
  without skips. The font fixture verifies real R8 coverage at 96/144/192 DPI,
  monochrome alpha, clipping, one 1 MiB texture, zero updates at 1000 unchanged
  upload boundaries, update reuse, device recovery and balanced font/GPU teardown.
  D3D debug/live-object checks pass. This is controlled recovery, not physical
  GPU unplug testing.
- Offscreen PPM artifacts are written into the build directory as
  `font-visual-96.ppm`, `font-visual-144.ppm`, `font-visual-192.ppm`. Visual review
  of the 192-DPI fixture shows readable Latin/accent, Arabic, CJK, monochrome
  emoji and ellipsis. Pixel-exact fonts are intentionally not pinned or shipped.
- Nine Release UI/benchmark tests pass. The font-enabled DX11 idle trace records
  3000ms with one blocking wait, zero Present/Map/Draw and no texture updates.
  Executable imports use DirectWrite and the C/Windows runtimes; no C++ runtime
  or runtime shader compiler is imported.

Reproduce the issue's normal ASan commands. To include offscreen checks configure
`-DENABLE_RENDERER_GPU_TESTS=ON`, build, and run
`ctest --test-dir build -R "^ui_(font|renderer)" --output-on-failure -V`.
Performance checks use a separate Release build and
`ctest --test-dir build-rel -R "^(ui_.*|bench_ui_.*)$" -V`.
