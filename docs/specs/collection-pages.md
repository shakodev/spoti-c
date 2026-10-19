# Bounded collection page service (#22)

Issue #23 supersedes the private buffers below with a single explicit pool shared
with catalog views. See [catalog-views.md](catalog-views.md) for current layout,
budgets and measurements. `collection_service_init` now receives that pool;
its own storage contains models and tickets only.

Written before implementation. This extends the predecessor's
[Home/Library/Playlist contracts](home-library-playlist.md),
[native backing contracts](native-library-playlist.md) and `pathfinder.md`.
No new endpoint, header, persisted hash or wire schema is introduced.

The additional generated Library module20609 was read through REA parent
`ev_46067037650350676ea1fecb03c83364d9bfedfb6fb14b5fe506ea79b16a2cd9`;
`collection-library-rea.json` retains the partial coverage and source identity.
Its entity variants include playlist/folder/album/artist, not a `collection`
variant. The adapter supplies its authenticated account's liked-songs URI via
local configuration; exact identity matching labels that playlist as liked.
No universal account URI or extra wire field is assumed. Local page size is
configurable from 1 to 64 before navigation; Playlist's echoed range length
must match it exactly. HomeSection may return more rows than requested, as
the independently captured limit2/20-row response demonstrates.

REA inspection on 2026-10-19 selected the issue's four original XPUI files.
Parent `ev_69a6f9c5a70c21e1cb880edb1cede0639ad90431517144128e3b2b704bf09af8`
pins tree SHA-256 `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Four files parsed without failures; semantic coverage is bounded/partial and
267 other files were not analyzed. The inspected playlist module reference
is unresolved: it does not establish an HTTP implementation.
The retained six-record predecessor HAR was independently read by REA,
parent `ev_5773c3cceb8dae873068fc9c84c53b1d91a4a06c3ffb527861936c1d26196baf`,
SHA-256 `a8647297e6eeb5b8a2eb6fa6954858bef1afcd9d8fe7485732db6dd28894aeaf`.
These are inert source and historical capture observations, not a live
compatibility test. Full returned projections and limitations are retained in
`collection-pages-rea.json`; predecessor native RPC evidence remains in
`api-contract-current-native-rea.json`.

## Local model and transport boundary

`collection_fetch` reserves one of two fixed page slots and returns an immutable
typed request and writable bounded response buffer to the network owner.
The request identifies the proven catalog operation and transport. The adapter
supplies session-specific variables/policies and delivers either PathFinder JSON
or the native generated-client JSON projection described by the predecessor.
It must never send a native RPC descriptor to WinHTTP. The module provides no
standalone Esperanto bridge or invented HTTP replacement. Native protobuf
decoding and backing-source synchronization remain separate transport work.

Home exposes shelves from `data.home.sectionContainer.sections.items`; a shelf
retains its `sectionItems` object for typed item access. HomeSection reads
`data.homeSections.sections[0].sectionItems`, including `pagingInfo.nextOffset`.
Root Home is not paginated. Library reads `header`, `entity`, `pinnedEntity`,
`statusCode`, and `error`. Playlist reads `status`, `data.item`, `data.playlist`,
`data.contentSignature` and the echoed `query.range`. Empty/default native rows
remain unavailable rows. Folder/liked/unknown entity variants stay distinct.
No revision is invented when the primary native projection omits it.
Recognized track/album/artist/playlist/episode URIs also expose their 16-byte
base62-decoded ID through the existing media ID codec. Namespaced URI spans and
playlist row IDs remain available independently; a missing/opaque ID never
becomes an invented binary identifier.

Generation changes invalidate pending publication, but their buffers remain
reserved until the transport completes/cancels. Ticket serials prevent a late
completion from targeting a reused slot. One fetch per generation is pending
at a time. Continuations must exactly match the accepted next offset; echoed
playlist ranges must match. Revisions, when provided, must remain equal across
continuations. An explicit changed-revision outcome requires a fresh generation.
GraphQL partial errors publish usable data with a PARTIAL outcome; no-data
errors, auth, rate-limit and malformed replies never publish a page.

Two input/pool pairs share the existing 1 MiB API-page allowance. Each input is
80 KiB plus yyjson padding, each DOM pool is 384 KiB. Models and service state
also fit this allowance (compile-time assertion). Pools have no malloc fallback.
Whitespace bodies exercise the body cap independently of object-heavy DOM
exhaustion. A page has at most 128 rows; exceeding either cap fails explicitly.
All immutable spans borrow retained input, never frame scratch. Atomic reference
counts permit UI retain/release; parsing/admission belongs to one network owner.

Duplicate identities are marked, preserving row position and repeated playlist
track URIs; playlist identity is rowId when available. Detection covers rows in
the page and overlap with the preceding accepted page while retained. This
service keeps no full-playlist JSON tree or unbounded global identity index.
Consumers needing an exact history for 10k rows must persist compact identities
outside these two pages (for example on disk); repeated navigation releases all
page references and returns occupied slots/DOM accounting to baseline.

## Verification measurements

MSVC x64 Debug/ASan reserves 977,472 bytes for the entire service, including both
body buffers, pools, page models and state. The observed predecessor Home body
parses to 7,951 DOM values (127,216 bytes of value storage); this excludes pool
allocator bookkeeping and spare capacity, which remain inside the fixed pools.
A valid 60,001-byte array of 30,000 scalar values exhausts a DOM pool and returns
NO_MEMORY without falling back to the heap. Thus input bytes alone do not
establish DOM admission. Oversize bodies and >128-row pages fail explicitly.

`api_collections` exercises predecessor positive/error/continuation fixtures,
synthetic folders/adapter-supplied liked identity, null/unknown unions, duplicate
rows and overlapping pages, missing/reordered offsets, changed revisions,
Unicode, partial GraphQL errors, auth/rate-limit statuses, malformed JSON and
pool exhaustion. Two threads concurrently retain/read/release a page after
navigation drops the service reference, with all created handles closed.
One thousand navigation cycles return occupied slots to zero; a synthetic
10,000-row playlist streams through 157 bounded pages without retaining the
full tree. These local fault/scale tests do not claim live service compatibility.
