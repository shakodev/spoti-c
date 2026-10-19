# Catalog and lyrics screens (#32)

Recorded before implementation. The existing catalog-views.md and
lyrics-timeline.md specifications remain authoritative for requests, headers,
persisted hashes, response shapes and audible-clock semantics. No endpoint is
added by these screens.

REA analyzed xpui-routes-search.js, xpui-routes-album.js,
xpui-routes-artist.js and xpui-routes-lyrics.js in ref/xpui. Summary evidence
ev_8eaffa881702692ac79a66394308ee3d13448618878995b3660726146763c9c9,
parent ev_2fa035a2e0d5625fc71395ace1fe91f8add72a55eba64336a8cc44a747f12931,
tree SHA-256 8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728.
Four files parsed (239696 bytes) without failures; 267 files were not selected.
Semantic coverage is partial and bounded by node limits, with unresolved
relationships. No JavaScript or network request was executed. The full returned
evidence and limitations are retained in catalog-screens-rea.json. This does
not establish live compatibility, exact Spotify debounce delays or scroll policy.

## Local composition and policy

Search, Album and Artist share one paginated CatalogPage view, stable compact
entity IDs, virtual rows, navigation history and existing PlayerCommand dispatch.
Artist top tracks and releases select existing local ArtistSection projections;
nonzero artist cursors remain unsupported. A page replaces the preceding page
rather than accumulating unbounded results. Loading, partial, boundary, missing,
restricted, offline and authentication outcomes remain visible.

Search uses a local 250 ms debounce and immediate route-generation cancellation
on committed query/category changes. One scheduler deadline dispatches the final
query. History stores bounded query/category/section and scroll values, never
borrowed page spans. Empty queries issue no request. IME/clipboard editing uses
the existing bounded text widget. These timings and layout choices are ours.

Lyrics retains one existing compact document and refreshes the existing timeline
from PlayerSnapshot.position_frames. Wall-clock wakeups never advance playback.
The next line schedules one invalidation. Manual wheel/keyboard scrolling holds
automatic centering for five seconds, then resumes through one finite deadline.
Hidden, paused, plain, capped, missing or mismatched lyrics have no line timer.
Track changes invalidate old deliveries, and seek recalculates highlighting in
either direction. Playing with no interaction retains only the existing 1 Hz
progress wake and the next audible lyric boundary. There is no animation loop.

At progress/lyric wakes, the desktop requests a coalesced decode-owner clock
sample. PlayerSnapshot.clock_serial acknowledges that sample; the decode owner
refreshes the sink timeline before publication and the headless host wakes the
UI for the acknowledgement. This avoids depending on an older cached position
when a short track is already fully buffered. It adds no playback command,
device query on the UI thread, timer or audio-callback notification. A refresh
arriving during a pump keeps owner work pending and cannot be lost to event reset.

All response storage uses the existing shared two-slot JsonPagePool. The view
holds only one route page or one lyric document. Delivery consumes references on
every outcome; the serialized host resets previous service-owned references
before switching services. No private JSON pool is added. Catalog/lyrics control
storage is charged to the unused portion of App's fixed 256 KiB UI-state
reservation, including when the fixture network owner operates on it. It must
outlive worker cancellation acknowledgement. App and fixture compile-time bounds
continue to enforce the total ledger; the playlist index keeps its 512 KiB model
reservation. Images go through the existing Artwork service and central LRU.

The explicit offline demo uses synthetic bodies conforming to the previously
verified parsers. Tests exercise real CatalogService/LyricsService rather than
inventing another endpoint/parser. Account and physical-device tests remain opt-in.
