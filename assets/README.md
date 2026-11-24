# Spoti-C icon

`spoti-c.png` is the original generated artwork. `spoti-c.ico` packages it at
16, 20, 24, 32, 40, 48, 64, 128 and 256 pixels for Windows. The app embeds the
icon as resource 101 and uses it for its window and executable.

Created with the built-in image generation tool. Prompt:

> Use case: logo-brand. Create a polished Windows desktop app icon for Spoti-C,
> an ultra-light native music player written in C. One single bold geometric
> letter C that also suggests a vinyl record / circular music groove with a
> small clean play triangle cut into its open right side. Vivid music green
> mark on a nearly black rounded-square tile, generous padding, crisp flat
> vector-like shapes, strong silhouette readable at 16 px. Minimal professional
> design. No text, no wordmark, no Spotify three-wave logo, no mockup, no shadows
> outside the tile. Centered square composition. Transparent outside the rounded
> tile. Deliver one icon only.

After building Release into `build-desktop`, run
`powershell -ExecutionPolicy Bypass -File tools/install-shortcut.ps1` to create
the current user's Start menu shortcut. Alternatively pass `-Executable` with
the path to another built `spotic.exe`. Press Win and type **Spoti-C** to find
the shortcut once Windows search refreshes. The shortcut points into this
checkout; rerun the script if you move it.
