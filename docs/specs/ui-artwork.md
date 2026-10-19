# Bounded asynchronous artwork (#28)

## Evidence recorded before implementation

REA `analyze_javascript_application` inspected exactly
`xpui-routes-album.js`, `xpui-routes-artist.js`, and
`xpui-routes-playlist.js` in `ref/xpui` on 2026-10-19.
Artifact SHA-256: `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Parent Evidence: `ev_bba7e6b7d6f36242f405f48598c56ae0c0a645255ed5539ac127ebdebff1aff3`;
summary: `ev_28c4eeeabbb0092638d4c458f506f4c2c60d78bd897a27fc02783f3bfac45d6f`.
The complete summary receipt is retained in [ui-artwork-evidence.json](ui-artwork-evidence.json).
Three files parsed, zero parse failures; 268 other files were not analyzed.
Semantic coverage is partial (per-file node ceiling). This is static evidence,
not an observation of network requests or codec availability.

Supplementary bounded source inspection of those same files found:

- Artist cards pass `e.coverArt?.sources||[]` and `placeholderType:"album"`.
- Album merchandise uses `e.image?.sources||[]`.
- Playlist models flatten `images.items` and copy each source's `url`, `width`,
  and `height`; track/artist mapping uses `albumOfTrack.coverArt` and
  `visuals.avatarImage` respectively.

These observations support copying an explicitly supplied source variant and
showing a placeholder when it cannot be used. They do not establish a URL
rewrite that converts WebP to JPEG, required image authentication headers, or a
server-side resizing API. The implementation invents none of these. It selects
only a supplied variant with known dimensions <=256, copies its HTTPS URL, and
uses no Spotify credentials. Unknown/oversized variants produce a placeholder.

## Implementation contract (local design, not Spotify behavior)

- Eight copied request slots; one blocking art-worker decode slot and one UI
  upload slot. `ArtworkId` carries content identity, requested size, navigation
  generation. Worker completion remains signaled until consumed; no queue-full
  completion loss, busy waiting, frame decoding, or frame allocation.
- One setup-owned 512 KiB workspace holds a <=64 KiB compressed body and the
  remaining bounded stb allocator. A separate <=256 KiB RGBA upload buffer is
  the only retained decoded copy. Original dimensions are checked before full
  decode (<=512 each, overflow-safe pixel product); output <=256 each. Allocator
  exhaustion is an explicit budget result, never an automatic heap fallback.
- stb_image is compiled for JPEG/PNG only. WebP takes a C COM WIC path with an
  actual decoder capability check, preflight dimensions, and bounded output.
  Missing WebP codec returns unsupported and the caller draws a placeholder.
  WIC/COM/WinHTTP internals are outside the stb arena: process private/peak
  memory and owned COM/handle counters are measured separately. Measurements
  are observations, not a hard allocator bound on installed codecs.
- Synchronous native WinHTTP runs only on the worker, with bounded URL/body,
  HTTPS, no redirects, explicit network timeouts and cooperative cancellation.
  Each job releases its own transport handles before completion. Test fetches
  are injected; default ASan tests need neither accounts nor GPU/network access.
- UI preparation uploads/releases; frame lookup only pins/resolves a handle.
  A 4 MiB logical GPU texel LRU uses serial handles, pins visible entries, and
  retires evictions only at the next preparation boundary after frame use.
  Pinned pressure returns full/placeholder. Device reset invalidates handles.
  Driver residency is separate from logical texture accounting.
  `artwork_begin_visible` starts a new visible set at a safe boundary; callers
  lookup/request that set before `artwork_prepare`. Otherwise pins persist
  across asynchronous completions. All-visible pressure retains one bounded
  upload for a later visibility change and renders a gray placeholder through
  `artwork_draw`; it neither re-fetches nor signals a continuous wake loop.
  A deferred eviction signals one additional preparation to release/upload.
- Navigation cancels obsolete requests. Pending/running storage stays alive
  until worker completion; stale results never upload. Shutdown must quiesce
  the worker before destroying its context or completion event.

## Verification plan

Default `ui_artwork`: synthetic JPEG/PNG/WebP, capability unavailable, malformed,
truncated, oversized, allocator exhaustion, cancellation and stale completion;
hundreds of cache insertions, pin pressure, deferred release, stale handles,
bounded request admission, and balanced explicit ownership counters. Optional
WARP checks verify real texture release and DXGI diagnostics. WIC process-memory
observations are recorded by a Release-only benchmark (no ASan distortion).

## Integration and limits

Allocate the opaque state using `artwork_size`/`artwork_alignment` and the two
fixed buffers at setup. Supply the shared art/cache `Worker` and an injected
fetch implementation (`artwork_http_fetch` accepts optional `Metrics*` context).
Copy model image variants into bounded `ArtworkSource` requests. Navigation
generations must increase. Add the borrowed completion handle to the UI wait
set. On a visibility change, after the prior frame has been consumed:
`artwork_begin_visible`, lookup/request each visible ID, `artwork_prepare`, then
build frames with `artwork_draw`. An unchanged visible set keeps its pins.
`ART_FULL` on worker admission can be retried after the worker's idle event.
On shutdown, keep buffers/state alive and retry `artwork_stop` after completion
while it returns pending; join the shared worker before reclaiming its arena.
The current foundation window has no API-backed album/artist screen; this
module exposes the reusable renderer/fetch/frame integration for those screens.

The 512 KiB workspace includes the reserved 64 KiB compressed input, leaving
448 KiB for stb allocations, alignment and ASan redzones. Useful tested sizes
include 128x128 RGBA PNG and 256x256 subsampled JPEG. A 256x256 RGBA PNG exceeds
this initial budget because stb temporarily holds filtered source and output;
it deliberately returns `ART_BUDGET` and displays a placeholder. Dimensions are
an upper bound, not a promise that every encoding fits. No decoder heap fallback
or retained original-resolution pixel copy is used. PNG chunk bounds/CRC and
JPEG segment bounds, scan presence and initial component coverage are checked
before full decoding. New stb blocks are zeroed because stb salvages some
damaged entropy streams; this is not a full JPEG conformance validator.

Transport uses 1 s native phase timeouts and a 5 s cooperative deadline, with
cancellation checks between synchronous native calls. It cannot preempt a
stalled OS/codec call. See Microsoft's [WinHTTP option documentation](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags)
and [synchronous read lifetime contract](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpreaddata).
No URL transformation, account credential or additional codec DLL is bundled.
WIC and any installed WebP codec are native system capabilities; COM activation
and driver memory remain outside the project's allocator ledger.

## Recorded verification (2026-10-19, Windows x64/MSVC)

- Strict `/W4 /WX` C17 Debug ASan and Release builds passed. Default CTest
  correctness suite and the explicitly enabled artwork WARP/HTTPS tests passed.
- Default `ui_artwork` includes 500 cache insertions, slot/byte/pin pressure,
  a 17-visible-image pipeline regression, no repeated fetch under pressure,
  cancellation, stale successful upload, and explicit allocation/handle balance.
  1,000 frame builds/ready and placeholder draws recorded zero CRT heap calls.
- Opt-in `ENABLE_RENDERER_GPU_TESTS=ON`: 400 real WARP navigation evictions,
  pixel goldens, device recovery, stale handles, resource counts returning to
  baseline and final DXGI live-resource diagnostics passed.
- Opt-in `ENABLE_LIVE_ARTWORK_TEST=ON`: native HTTPS fetched the pinned stb
  license (2,510 bytes) and rejected the pinned oversized stb header. All three
  WinHTTP handles retired; counters balanced. This verifies public HTTPS only,
  not Spotify account/image-host compatibility.
- Release `bench_ui_artwork_wic`: 100 8x4 WebP decodes then 100 512x512 decodes
  resized to 256x256. Small-image process private baseline/final:
  1,294,336 / 3,145,728 bytes. Maximum-size final private: 2,990,080 bytes;
  process lifetime peak: 4,210,688 bytes. Both runs had zero retained growth
  after ten warmup decodes and zero owned COM objects after every decode.
  These are process-wide observations including caches and unrelated native
  allocations, not a universal codec allocator cap or an application RSS claim.
- Release PE dependency inspection found the C runtime and explicit Windows
  APIs, with no C++ runtime dependency. The upstream stb header hash matches
  `vendor/manifest.json`; its existing license is preserved byte-for-byte.
