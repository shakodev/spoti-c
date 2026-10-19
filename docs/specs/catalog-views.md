# Typed catalog views (#23)

Recorded before implementation. This extends `catalog.md`, `pathfinder.md` and
`collection-pages.md`; the request documents, headers and endpoint are unchanged.

REA inspected the four issue-selected XPUI files under parent
`ev_d16b170c79b9e38c934fa361aca399675a333c105b43896916a341055ed421e1`,
tree SHA-256 `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
All four parse; semantic coverage is partial/bounded and 267 files are unselected.
REA additionally inspected the predecessor's exact snapshot factories2410,
36920,41231 and shared transport factories62037,32836,6854,69677,95096 under
parent `ev_63212a620e65e70207eda539fe5acad7104c289d28db596c35e0f45b3c877a7b`.
Eight files parse without failures; 2369 other files remain outside scope.
The replay HAR was read by REA parent
`ev_715bacdcb515b002bdc22a20a2583877dba8793b2af80aae233ef658811e2382`,
SHA-256 `b1d51bc6dddd31bfdd64e89bbee462a03dc650c73d5caad5435741bccd82561c`.
These are inert source/historical captures, not current live compatibility.
Complete returned summaries, identities and limitations: `catalog-views-rea.json`.

## Requests

All requests use the canonical `ApiContract`: URL, POST method, header allowlist,
operation and persisted hash are selected together. Required fields are emitted
from that contract's variable list. SearchDesktop/Tracks use `searchTerm`;
TopResultsList uses `query`. Feature flag values follow the independently
captured requests in `tests/fixtures/api`; no new category document is invented.
Desktop can select its embedded albums/artists collections locally. Page limits
must be 1..100. Album uses getAlbum at offset0, queryAlbumTracks for continuation,
with uri/locale/offset/limit. Artist uses queryArtistOverview with
uri/locale/preReleaseV2; its main-route hash differs from the3308.js constructor.
The source-local app-version896000000 override belongs in the contract layer
for Album/Artist only. Tokens remain owned by the authenticated transport adapter.
Request upload and response input are separate retained buffers. UTF-8 and JSON
escaping use yyjson with a bounded caller allocator and no heap fallback.

Search debounce is a local UI policy, configurable in milliseconds. New query
generations invalidate publication immediately, even before the debounce deadline.
The owner supplies monotonic time and schedules one deadline; no polling thread.
Cancellation does not reclaim a buffer still held by transport. Completion must
report the executed document hash; a mismatch is an explicit failure, never a
guess at a replacement hash. This is transport metadata, not an invented JSON
response field. Equal-generation query/category/limit changes are rejected.

## Response mapping

Search reads data.searchV2. Tracks use tracksV2.items[].item; Album/Artist
categories use albumsV2.items[]/artists.items[]. Top results use
topResultsV2.itemsV2[].item. Wrapper and data.__typename must agree for supported
Track/Album/Artist rows. Null/error/unknown variants remain unavailable or
unsupported rows. Each collection's totalCount/pagingInfo applies only to that
collection. A missing cursor uses bounded offset/count progress, never another
category's count. Empty rows terminate; a repeated/decreasing explicit cursor
stops with a boundary result. List top results have no aggregate total; full
pages can continue until an empty/short page. This is local admission policy.

Album requires albumUnion.__typename Album. tracksV2.items[].track is a direct
track projection with uid and may omit __typename (observed), rather than a
Search TrackResponseWrapper. AlbumTracks may omit Album name/uri/cover metadata.
Artist requires artistUnion.__typename Artist. The requested local section
selects overview, topTracks, albums, singles, popularReleasesAlbums or related
artists. Release groups retain bounded album entries. The overview request is
not paginated: nonzero cursors and unsupported sections return UNSUPPORTED;
truncated nested sections expose their total and no invented continuation.

Track/Album/Artist form a tagged union with borrowed UTF-8 spans, source metadata,
16-byte compact Spotify IDs, availability/restriction state and playable track
URI/ID. Relinking information remains distinct from original identity. Image
selection examines at most32 sources and chooses the smallest sufficient width,
otherwise the largest known width; missing dimensions are fallback candidates.
Too many sources/rows fail explicitly. Unknown variants do not become tracks.
Partial GraphQL errors publish usable typed data with PARTIAL; null/error root
unions never publish success. No request or error fixture is treated as SDL.

## Shared ownership and bounds

Collections and catalog receive the same explicit JsonPagePool. Its only two
slots each reserve140 KiB padded input plus368 KiB DOM pool; their aggregate
storage is below the existing1 MiB API-page allowance. Models/requests live in
separate caller storage with a combined compile-time512 KiB bound. Neither
service owns an additional private page pool. At most two buffers are reserved
or retained across both services; a third request returns FULL. Existing
JsonPage atomic retain/release semantics remain authoritative across UI threads.
Final release poisons borrowed input and DOM under ASan; acquisition unpoisons
only its exclusively reserved slot. Model memory remains accessible for safe
reference accounting. Stale/cancelled completions release their reservation only
after the transport acknowledges termination. No frame scratch or heap growth.

The larger body cap admits the predecessor's138,760-byte List response. Its
19,221-value fixture envelope demonstrates that input bytes alone do not predict
DOM admission: yyjson initially estimates values from minified length/6, which
exceeded the first336 KiB pool split. The adjusted368 KiB pool admits that
unchanged captured response. Object-heavy
inputs can exhaust the DOM budget earlier and return NO_MEMORY. Local caps do
not claim server maximums. Collections' historical measurements remain valid
for #22; this shared-pool layout supersedes their private buffer arrangement.

## Verification

MSVC x64 Debug/ASan measurement: the shared pool is1,040,416 bytes; catalog
model/request storage is79,888 bytes and collection model storage is27,224 bytes
(107,112 bytes combined). Compile-time assertions enforce the1 MiB/512 KiB
ceilings. The unchanged Desktop/Tracks/List positives parse respectively12,807,
655 and19,127 DOM values, with2/2/50 selected rows. Sanitized URI strings remain
opaque rather than becoming invented playable IDs; synthetic known base62 IDs
independently verify exact16-byte decoding and availability restrictions.

The deterministic `api_catalog` target checks each operation's positive/error
fixture, continuation/exhaustion, AlbumTracks' distinct name despite equal hash,
all supported local Artist sections, null/restricted/unknown unions, empty search,
Unicode/escaping and invalid UTF-8,100-row and32-image-source bounds, reordered
cursors, repeated nextOffset, debounce/cancel/stale tickets, transport hash
mismatch,401/429/503 and body/DOM exhaustion. Fixture response projections are
replayed locally; their recorded request offsets are not claims of newly sent
service requests. Request envelopes independently check canonical document/hash,
required variables, searchTerm/query separation and header metadata/overrides.

A collection page and catalog page exhaust the same two reservations, and a
third request fails FULL. ASan verifies a borrowed name stays readable through
retains and becomes poisoned only after final release. Navigating with a URI
borrowed from the sole service-owned page copies the URI before invalidation.
One thousand navigation cycles restore shared reservations to zero. Existing
collection concurrency and10k-row bounded-pagination checks also remain enabled.
These fixture/fault tests do not claim current live-account compatibility.

Required final verification passed: Debug configure with ENABLE_ASAN=ON,
`cmake --build build` under MSVC C17 `/W4 /WX /fsanitize=address`, and
`ctest --test-dir build --output-on-failure`:39/39 deterministic tests passed,
with no compiler warnings or sanitizer errors. No live account/device tests
were enabled. The module owns no OS handles or heap allocations; reservations
and test-created thread/event handles return to baseline through the existing
collection concurrency checks and shared-pool lifetime assertions.
