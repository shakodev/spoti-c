# Detail route interactions (#43)

Recorded before implementation on 2026-10-20. This is an incremental interaction
contract; desktop parity still depends on the shared reference/components (#41)
and shell (#42), both open at the start of this work.

## Evidence and limits

REA MCP `analyze_javascript_application` inspected exactly the playlist, search,
album, artist and lyrics route scripts in `ref/xpui`. Parent evidence:
`ev_cba03c5f4d5cadcbbbcda5c7edc0bda21be13fedf10766700811acf1b9c1f4c6`;
tree SHA-256 `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Five scripts, 309362 bytes, zero parse failures. The other 266 files and
relationships through them were not analyzed. Semantic coverage is partial and
bounded. The lyrics route is only a 384-byte loader, SHA-256
`7f8395f754ae34e66ef3cf92829a7960bab0382f109ba6249ab77cbd473796d8`;
it does not establish lyric typography or follow behavior. An outgoing `scroll`
string trace found no matches; this is not evidence that scrolling is absent.
Returned evidence is in [detail-routes-rea.json](detail-routes-rea.json).

With explicit user approval, Computer Use opened the installed Spotify desktop
app and inspected loaded Playlist, Artist, and Search routes. The installed
artifact is 1.3.5.3058, while the extraction manifest pins 1.3.4.258; neither is
asserted to be the latest. Artist has a full-width image hero and Popular tracks;
Search has category pills, a prominent artist result, shelves and mixed rows.
The native compact detail-cover header remains an explicitly incomplete visual
implementation rather than a claim to reproduce that artist hero.
At the observed 1600x900 capture the main pane has a cover/title/metadata hero,
a primary Play action above the table, search/order controls, and columns for
ordinal, title with artist below, album and duration. Rows expose distinct play,
artist/album navigation and more-options actions. Exact breakpoint/DPI behavior,
menus and keyboard timing are not established by this single observation.

No API endpoint, persisted hash, header or JSON shape changes here. Existing
catalog-views.md, playlist-screen.md and lyrics-timeline.md remain authorities.

## Native rules to implement and test

- Primary playlist Play must work without an existing selection: use the first
  playable occurrence in the current filtered/sorted window. A visible playable
  selection takes precedence. Empty/restricted/pending sources disable Play.
  Preserve revision/ordinal identity, bounded windows and copied player queues.
- Album/Artist use an explicitly labeled Play first track action that finds the
  first available track in the current page. A page containing releases alone
  cannot imply album playback or a complete album queue.
- Catalog headers show the existing immutable detail metadata and real detail
  artwork through the existing bounded artwork service. Artwork preparation
  precedes layout; route generations retire stale images. Continuation pages
  lacking detail metadata must not erase the route title.
- Track rows visibly identify the current compact player ID. Match against the
  consumed player snapshot, never title text, row position or a predicted clock.
- Keyboard Enter on an initially focused catalog/playlist list must activate a
  deterministic first row. Arrow navigation selects stable identities; pending
  pages reject playback. Page replacement must reject stale generations.
- Lyrics retain consumed-frame highlighting and five-second manual follow hold.
  Typography changes do not introduce animation ticks or clock extrapolation.
- Service failures name unsupported/restricted/rate-limited/missing outcomes;
  no invented recovery action or fixture substitution establishes compatibility.

These native choices are testable local policies, not claims of exact Spotify
runtime parity. The missing #41 matrix, shared visual foundations, shell layout,
playlist artwork adapter and complete live route acceptance remain explicit
acceptance work. Issue #43 must remain open until those requirements are met.
