# Virtual Playlist / Tracklist (#31)

Evidence recorded before implementation, 2026-10-19. REA MCP analyzed exactly
`xpui-routes-playlist.js` and `xpui-routes-collection-songs.js` in the extracted
XPUI directory SHA-256
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Parent evidence:
`ev_f2f2f5bbe96cc7e5349c371356b7a9c51204b6c67ba8e8c0fe029489438f791a`.
Two scripts parsed, 126368 bytes, zero parse failures. Application and semantic
relationships remain partial; 269 files were unselected. The analysis executed
no JavaScript and made no network requests. The portable summary is retained in
[playlist-screen-rea.json](playlist-screen-rea.json).

REA module views `ev_c91f81df5b4337d69f922fe4274a92f2aa782a7d3aedbb03ac2a72c49b2708d8`
and `ev_8431dc3fdf526bb59c3f4778d2cb90b33a57e79858eba0becf3395391a32a11f`
identify script SHAs `ba01d3c8d15eda04889d5087274b7ee7f07511fd0f447897e3a5ab9e6671e9f5`
and `4336244db7731e47cea780328d302ff67e3b20092e423b823bc97ddc53b280b9`.
Those particular views expose unresolved imports 36839/68911, not recovered
implementations of filtering. Spotify's exact collation, debounce timing,
virtual row count and queue policy remain unknown. The native rules below are
design choices, not assertions about Spotify behavior.

The existing [primary playlist contract](home-library-playlist.md) remains the
API authority: native PlaylistDataService.Get ranges, distinct row occurrences,
contentSignature revisions and policy-dependent decorations. No new endpoint,
query hash, JSON field or undocumented RPC transport is introduced. Historical
`ref/cspot/cspot/src/TrackQueue.cpp` is consulted as source context for compact
track identity versus separately loaded metadata; its C++ ownership is not used.

## Native ownership and budgets

- One immutable source revision supplies bounded normalized records through a
  worker-only reader. Production adapters populate it using the existing API
  contract. No borrowed JSON pointer survives page ingestion. The offline disk
  index is a private versioned local format, not a Spotify wire format.
- At most 10000 compact ID/availability/duration entries, two 10000-entry
  result maps, one cached sort-order map and bounded radix-sort keys are resident.
  A bounded visible metadata cache copies strings
  into a fixed offset-addressed table. Filter workspace, metadata, model/maps
  plus the existing 64 KiB collection arena must fit the shared 512 KiB model
  reservation. App/widget state retains its separate 256 KiB cap.
- Longer sources stay on disk and expose explicit 10000-row index windows.
  Window navigation has no full-list allocation. A window search is labeled
  as such; it is never presented as a complete search of a larger playlist.
- One bounded worker job reads/filter/sorts or loads visible metadata. UI
  request generations and source revisions reject stale completion; cancellation
  must acknowledge before the caller releases/reuses a source. No synchronous
  disk/network access, shaping or heap allocation occurs in layout/render.

## Filtering, identity and playback

- Query cap: 96 UTF-8 bytes including terminator. Invalid UTF-8 and overflow
  fail transactionally. Matching is substring across title, artist or album,
  folds ASCII A-Z only, preserves other Unicode scalars exactly, and performs
  no normalization, accent removal or locale-dependent transformations.
  An empty query includes known IDs even when metadata is not loaded.
- Results report full-index, metadata-loaded-only or current-window coverage.
  Missing decorations and a partially downloaded source cannot claim complete
  filtering. The UI labels pending results and disables actions against them.
- Sorting uses source order, title or duration with original ordinal as the
  deterministic tie-breaker. Selection/context identify (revision, ordinal),
  not song ID or visible slot: duplicate songs remain separate occurrences.
  Filter/sort preserve a hidden selection; revision changes invalidate it.
  A source/revision/window/sort-keyed order cache avoids disk comparisons on
  subsequent keystrokes. The initial title sort is worker work and has its own
  measured cost; it is not included in a warm filtering latency claim.
- Visible rows plus bounded overscan alone create Clay elements. The table
  renders title/artist/album/duration and visible loading/unavailable states.
  Keyboard and pointer operations select the same occurrence.
- Publishing a new visible metadata slice invalidates Clay's old word cache.
  The four-column table needs more simultaneously shaped strings than Home.
  Rebalance the existing font CPU cache to 64 runs and seven pending requests;
  runs, requests, atlas descriptors and UTF-16 staging together remain <=256 KiB.
  The existing 256 KiB raster scratch and 1 MiB atlas caps remain separate.
  Preparation may take multiple bounded batches; warm frames must then stop
  requesting text and allocating.
- Playback creates a bounded copied queue of IDs with source URI, revision
  and original ordinals. The player receives an existing PLAYER_TRANSFER command,
  synchronously copied on submit. No queue points into metadata or API pages.
  The finite 64-track player queue is explicit; it is not a silent 10k queue.

## Implemented boundaries

`TrackFilter` owns one job with atomic running/completed publication. It scans
64 normalized records per read, validates bounded UTF-8 fields, and returns
generation-tagged ordinals. Its cached title order uses stable byte-wise radix
passes with the documented ASCII fold; duration sorting uses compact numeric
entries. Cache keys include source reader/context, URI, revision, length, window
and sort. A small match bitset filters the cached order without random disk
comparisons. Model mutation and playback admission remain disabled until a
matching result is published. Old source readers must outlive cancellation
acknowledgement; generation checks are not a substitute for ownership.

`Playlist` holds the UI map and at most 64 copied metadata rows in a 24 KiB
string table with uint16 offsets. Only the visible range plus two-row overscan
is requested. The viewport is capped at 360 DIPs; text outside the visible
range is not shaped. Noninteractive clipping boxes reuse bounded viewport
slots, while semantic row/button IDs retain original occurrence identity.
`ui_widgets_begin` retires Clay clip bookkeeping without driving scrolling.

Playlist/filter/metadata/order-cache state plus the existing 64 KiB collection
arena occupies 511416 bytes on verified x64 builds, below 512 KiB. A separate
compile-time check includes all offline host descriptors and its queue/disk
handle state within that same reservation. App/widget state is 148248 bytes.
The shared JSON pool remains in its existing API-page reservation; no playlist
JSON pages are retained for scrolling or filtering. Hosts must share/reuse the
model reservation rather than allocate another 512 KiB index per screen.

The private disk v1 format has a 280-byte Windows little-endian header and
264-byte records. Header magic, record size, total length and terminated
identity fields are checked on open; records are validated as the worker
reads them. It is a disposable normalized index, not a durable startup cache
or an alternative Spotify protocol. Writing, opening and reading are prohibited
on a GUI owner. The offline host creates a temporary 10005-track fixture on its
existing worker, publishes a `TrackSource`, and removes it only after joining.

Home/Library playlist selections now enter APP_PLAYLIST. The source host answers
the existing COLLECTION_PLAYLIST request by publishing `playlist_source` for
the matching route generation. Text, clipboard and IME input use existing
widget editing functions with the smaller query cap enforced transactionally.
Order/Title/Time cycles sorting; Tab, arrows, Home/End, Page Up/Down and Enter
operate on persistent occurrence keys. Hidden/unavailable selections cannot
activate Play. Failures require explicit retry and do not create polling loops.

`PlaylistQueue` contains up to 64 copied IDs, original ordinals, URI, revision
and a `more` boundary flag. The optional host callback copies this provenance
and submits the existing player transfer. Direct Player adapters still receive
only copied IDs, without borrowing metadata; queue-context retention belongs
to that playback adapter. The offline host retains the full queue descriptor.
No full-playlist auto-refill is claimed beyond the finite player queue.

[Verification, performance and screenshots](../verification/playlist-screen.md)
record tested scope. Live PlaylistDataService integration still needs a host
for the previously documented native bridge; this screen adds no live endpoint.
