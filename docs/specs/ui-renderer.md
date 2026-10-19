# Fixed-memory DX11 renderer (#26)

Evidence recorded before implementation, 2026-10-19. This is a new renderer for
the Clay command contract, not a reconstruction of Spotify's GPU implementation.
No Spotify endpoint is introduced.

REA MCP `analyze_javascript_application` selected
`ref/xpui/pip-mini-player-snapshot.js` in the pinned XPUI directory
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Parent evidence `ev_281f5d89d1afb5eb927c32c6e9472e48c7e52f8fd1e896624e457751c0ae5c06`;
summary `ev_237f5ec9556204d16e09fc299c7fdeb54d2a55a4fb0daf310017df3d6af14598`.
One JavaScript file parsed without failure; 270 inventoried sources were
unselected, and the semantic graph reached its per-file ceiling. These results
establish artifact identity and partial static coverage, not native GPU behavior.

Supplemental direct CSS inspection: `ref/xpui/xpui-snapshot.css`, SHA-256
`fdc9112f7a179e06889ddf3f1f2e9d2c3f2f3860339b17b6b4bae03affcebea5`, uses
`border-radius:8px` on desktop panels. CSS is visual
reference only. Our signed-distance shader, shadow falloff, batching and texture
ownership are explicit local design choices. There is no evidence that Spotify
uses this shader or these resource limits.

## Contract

Use C COM vtables, build-time FXC shader bytecode, and a caller-owned fixed
staging array of 4096 96-byte instances (384 KiB). A maximum of 1024 contiguous
batches and 32 nested clips preserve painter order. No sorting. Clip rectangles
are intersected in logical coordinates, then floor/ceil converted to physical
scissors at the requested DPI scale. Overflow/invalid/unsupported commands fail
explicitly; a failed frame cannot be submitted until reset.

Each instance has bounds, straight RGBA tint, UV rectangle, four corner radii,
four border widths and mode parameters. Shader output uses premultiplied alpha
and source-over blending. Images use straight RGBA textures; glyphs use the red
coverage channel. Border coverage subtracts an inset rounded shape. Analytical
shadows have finite support (maximum blur 64 logical pixels), expand the quad by
that support and never draw outside it. Shadows are submitted explicitly before
their corresponding shapes. Text shaping/atlas population belongs to the next
font module; glyph quads already share the texture pipeline.

Clay rectangles, borders, images and nested scissors translate directly. Text
and custom commands go through an explicit caller callback, which may append
glyph/shadow instances; absence returns unsupported. Images resolve caller data
to a bounded texture handle. No opaque Clay pointer is assumed to be a COM object.

Device setup, texture creation/destruction, resize, recovery and readback are
resource-management operations outside frame submission. Submit maps one bounded
dynamic instance buffer with WRITE_DISCARD, uses one constant buffer and draws
contiguous batches. No application heap allocation or resource creation occurs
in begin/append/submit. Texture table: 16 slots, 16 MiB total GPU texel budget;
stale generation handles are rejected. Device loss invalidates all texture
handles; the owner repopulates images/fonts after explicit recovery. Statistics
count owned COM resources and requested buffer/texture bytes, with a separate
two-buffer swapchain size estimate. Driver residency and shader implementation
memory remain unknown. A zero-sized resize retains resources and returns
suspended; the window owner suspends submission without busy retry. Hardware
setup may fall back once to WARP; failures remain explicit.

The window sink submits/presents only when the scheduler is dirty. DXGI
occlusion notification wakes the window through a message; no occlusion polling.
Resize unbinds and releases old targets before ResizeBuffers. Recovery is an
explicit owner operation outside submit. A persistent failure uses the existing
GDI fallback after at most one recovery; it never loops attempting device
creation while idle. Shapes use half a physical pixel of analytical edge support;
finite shadows use their explicitly bounded support.

Default tests use CPU batching/fake submission under ASan. Optional WARP tests
are enabled only with `ENABLE_RENDERER_GPU_TESTS=ON`; unavailable WARP returns
77 with a reason. Golden pixel samples cover shapes, premultiplied blending,
textures/glyphs, clipping/order and multiple DPI scales. Resource teardown is
counted, with DXGI/D3D debug live-object reporting when the debug layer exists.

The Clay header and license match the pins already recorded in
`vendor/manifest.json`: header SHA-256
`54aef6d5f96b14e296bebb6db4a755543e98fd7e6c188411afb2f22a4b4f7728`,
license `5dd30f9f52093076b7574458f5efb8242fe6cbbd4669d4a2914e7616810d0df8`.

Platform contracts: [dynamic buffer mapping](https://learn.microsoft.com/en-us/windows/win32/direct3d11/how-to--use-dynamic-resources),
[DXGI buffer lifetime and resize](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi),
[flip presentation](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model),
and [occlusion notification](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nn-dxgi1_2-idxgifactory2).

## Verification (Windows x64 / MSVC, 2026-10-19)

- Debug `/W4 /WX`, `/fsanitize=address`, FXC `/WX`: all 43 default CTest tests
  passed with `ENABLE_RENDERER_GPU_TESTS=OFF`. CPU tests count zero CRT heap
  calls while constructing/dispatching batches and translating Clay commands.
- Explicit opt-in Debug ASan `ui_renderer_warp` and `ui_renderer_window` passed,
  without skips. WARP's D3D debug layer was enabled. Tests check fixed RGBA
  goldens at scales 1/1.5/2 for per-corner rounding, fractional edge AA,
  asymmetric and zero-width borders, finite shadows, image UVs/alpha, glyph
  coverage, nested clipping and translucent painter order.
- GPU tests exercise texture-slot overflow, stale handles before any map,
  repeated resize and explicit removed-device invalidation/recovery. The HWND
  harness exercises Present, resize, a DXGI wake message, one recovery and
  persistent-failure fallback. This is controlled invalidation, not a physical
  GPU unplug/driver reset test.
- D3D live-child/error-message checks and the owned-resource ledger pass at
  teardown. DXGI live-object reporting is invoked when available. This does not
  claim to measure driver-owned residency.
- All seven selected Release UI/benchmark tests passed. `bench_ui_dx11` recorded
  3000ms with one blocking wait and zero additional Present, Map or Draw calls.
- Release executable imports contain neither D3DCompiler nor a C++ runtime.
  CPU `test_renderer` imports no graphics DLLs. Physical GPU behavior, HDR/color
  management and font shaping/atlas production remain outside this module.

Reproduce the normal ASan commands from #26. For optional checks configure with
`-DENABLE_RENDERER_GPU_TESTS=ON`, build, and run
`ctest --test-dir build -R "^ui_renderer" --output-on-failure -V`.
For performance use Release and
`ctest --test-dir build-rel -R "^(ui_renderer.*|bench_ui_dx11)$" -V`.
