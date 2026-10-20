# Readable status messages (#40)

This is a local UI contract. It changes no Spotify endpoint, protocol or inferred
Spotify behavior. The exact reported regression string, supplied by the local
host, is `Personalized Home is unavailable. Showing your playlists.`

## Findings before implementation

`main.c` configures the shared font context with `max_width=260` and
`ellipsis=true`. `app_text.c` inherits that key for Clay measurements, including
word probes. `text_cache.c` inherits it again for rendering. `font.c` disables
DirectWrite wrapping and installs ellipsis trimming; consequently Clay receives
already capped advances and the renderer paints an already trimmed glyph run.
`app_label` also disables Clay wrapping. Changing only the label width does not
repair either the measurement or the pixels.

## Contract

- Shared status components use a separate text role. Its measurement and drawing
  keys always disable trimming and width caps. Clay wraps to the pane's available
  width with a 20-DIP line height. Labels retain their existing trimming policy.
- Information, Warning and Error have visible textual labels as well as different
  backgrounds and colors. Inline notices occupy at most 60 DIPs of text height;
  clipping stays within the pane and cannot cross the navigation/player bounds.
- The pane header always offers **Read messages**. Tab and Enter open an owned
  native dialog containing all current notices, with a selectable, read-only,
  vertically scrollable multiline edit control. It wraps unbroken tokens and RTL
  text using Windows text services. Tab reaches Close; Escape/Enter close it and
  restore the parent. Resizing and WM_DPICHANGED resize controls and their font.
  Full details are available even if the inline copy is clipped by a short window.
- The account notice holds at most 1023 UTF-8 bytes. The setter validates UTF-8
  and embedded NULs, and replaces oversized/invalid input with an explicit readable
  capacity/encoding message and recovery advice. No partial diagnostic is stored.
- A fixed 4096-byte frame transcript holds all notices. Capacity exhaustion is
  reported explicitly in the transcript. Messages exceeding 64 UTF-8 bytes,
  two explicit lines, or a 32-byte unbroken token
  use an inline instruction to open Read messages; the full supported input stays
  in the transcript. A separate fixed snapshot survives dialog dispatch.
- Font measurement does not rasterize glyphs. Render preparation tiles wide
  glyph masks within the existing atlas/scratch/patch budgets. Neither layout nor
  rendering allocates; native dialog/font creation happens only on activation.
- No new timer or polling path is introduced. The existing state/input wakes
  handle notices; settled text must queue no further work.

The Release artifact was subsequently identified with REA MCP
`inspect_pe_resources`, evidence
`ev_8936994ad666c37c60700a470fd4ae9ddb924988c9581ddbcbd9cdf95006d65f`.
It is a 1,007,104-byte PE32+ AMD64 image, SHA-256
`7784ad2c7851b4a9234430604907d3f18b3017eb1fb54f8e2b1ba5d0e73a435b`.
[Full receipt](status-messages-release-rea.json). This static identity/resource
inspection establishes neither runtime behavior nor imports; those have separate
tests and audit evidence. No Spotify binary or frontend was needed for this UI fix.

## Verification required

Deterministic tests cover the exact Home notice, short/long/multiline UTF-8,
RTL, unbroken tokens, overflow/invalid input, keyboard activation, bounded height,
resize/DPI, and allocation-free settled frames. Explicit GPU tests inspect actual
DX11/DirectWrite pixels and save before/after captures. A separate native opt-in
checks the details dialog's real text, line count, scrolling, DPI/resize and
keyboard close. Account/audio-device acceptance is outside this UI change.
