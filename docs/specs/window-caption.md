# Native caption ownership (#37)

Recorded before the frame change, 2026-10-20. This fixes this application's
Win32 frame and adds no Spotify behavior or API. No Spotify artifact inspection
or reverse engineering is needed: all relevant implementation source is present.

## Diagnosis

`window.c` handles `WM_NCCALCSIZE` by restoring the original top edge after
`DefWindowProcW`, making the caption part of the client area. It extends the
DWM frame into that area and supplies caption hit-test values. None of those
operations draws glyphs in the application's render target.

`ui_surface_create` sizes its HWND swapchain to the entire client rectangle.
`ui_surface_frame` clears that target with opaque black and submits app content
without any caption-button draw commands. Thus the renderer paints the same
region intended for the DWM caption. Hit-test success cannot prove visibility.

The opt-in native `test_caption_visual` harness was built against the unchanged
frame and run with Debug ASan, real visible HWND and WARP DX11 on the desktop.
Its composed-screen capture `.cache/caption-before/default-96.bmp` was inspected:
the application title is visible on black, and all three caption glyphs are
absent. Narrow, wide and maximized captures were also retained locally. Actual
window DPI reported by Windows was 96; this is not evidence of other DPI scales.

## Chosen ownership contract

Restore the standard `WS_OVERLAPPEDWINDOW` nonclient caption. Let
`DefWindowProcW` own nonclient layout, drawing, hit testing, activation and pointer
states. Remove both the client extension and the guessed caption-button geometry.
Use the supported DWM dark-caption preference when available, with the normal
native caption as fallback. Keep the app's existing header inside the client
rectangle; client DX11 and fallback GDI rendering cannot cover native controls.

The OS supplies minimize, maximize/restore, close, caption drag/double-click,
resize borders, system menu, Alt+F4, accessibility, and snap where supported.
`WM_CLOSE` continues to mark the window closed and leave resource teardown to
the owning application's existing shutdown sequence. Maximize retains its
ordinary window style and is not fullscreen. DPI changes still apply the
suggested rectangle and invalidate the client surface; native caption metrics
follow the actual monitor DPI. No allocation, timer or render deadline is added.

## Regression and acceptance evidence

Tests must compare real caption-button rectangles, native hit tests and client
boundaries in normal/maximized states at narrow/default/wide widths; verify the
system commands, activation, orderly close and DPI notifications. Separate
opt-in composed-desktop captures are required because swapchain readback omits
the nonclient frame. Record actual DPI, never label synthetic DPI messages as
physical monitor transitions. The 100/125/150/200% visual matrix and hardware
acceptance must state any unavailable desktop configurations explicitly.

## Final artifact identity

REA MCP `inspect_pe_resources` inspected the isolated Release `spotic.exe`
after verification. Evidence
`ev_fdf4352f98f1aab11ccc33a20cb0c9e3f53bb197558741f70c17ce7875401a82`,
SHA-256 `cc660e97eb7aca1a769d8db9452b63df58b4a7ccbe418cbf536eb6c22590d6dd`,
989,184 bytes, PE32+ AMD64. The complete
[receipt](window-caption-release-rea.json) establishes static artifact/resource
identity only. Runtime drawing and pointer results come from the separate
native capture harness, not this resource inventory. The existing Release audit
separately verifies dependency imports without attributing them to REA.
