# Home, Library and primary Playlist contracts

Evidence/source identities and shared headers are defined in `pathfinder.md`.
All observations concern desktop 1.3.4.258, not Web Player compatibility.

## Home

Recovered snapshot modules 26662/83314 declare `home`, query hash
`76243c78b0e20ecdbe41b794dec8cbe73f75e585b0a7201b8d2e84578412847a`.
POST the PathFinder v2 envelope. The observed variable object has:
`homeEndUserIntegration` (string), `timeZone` (IANA zone string), `sp_t` (string,
empty is observed), `facet` (string, empty is observed), `sectionItemsLimit`
(unsigned integer, observed 10), `includeEpisodeContentRatingsV2` (boolean).
Do not infer that a supplied field can be omitted or changed to null.

The observed response is `data.home.__typename: HomeResponsePayload`,
`greeting.{transformedLabel,translatedBaseText}`,
`sectionContainer.{uri,sections.{totalCount,items}}`, and `homeChips[]`.
Sections and their item wrappers carry independent discriminants; consumers must
handle unknown kinds, missing/null optional fields, errors and partial data.
This is an observed consumer contract, not recovered GraphQL SDL.

`homeSection` and `homePinnedSections` use the same hash in module 26662; they are
distinct operation names, not aliases whose response can be assumed identical.
`xpui/home-v2-section.js` supplies `uri`, `homeEndUserIntegration`, `timeZone`,
`sp_t`, `sectionItemsOffset`, `sectionItemsLimit` (default 20), and
`includeEpisodeContentRatingsV2`. It checks `data.homeSections.__typename ==
HomeSectionCollection` before advancing the item offset. Pinned Home supplies
`uris[]` and `sectionItemsLimit` (default 10). These continuation responses and
the full section-item union have not passed the fixture gate.

## Library

Recovered modules 90879/82379/10608 establish a **native local RPC**:
`spotify.your_library_esperanto.proto.YourLibraryService`, `All` (single response)
or `StreamAll` (streamed). The generated client protobuf-encodes its request and
decodes its response; module 10608 forwards it through `executeEsperantoCall`.
This is not an HTTP request and is not a PathFinder document.

The native framing uses three length-prefixed byte strings: service name,
method name, protobuf payload. `DataView.setInt32` is used without a little-endian
flag. JavaScript/CEF supplies the native callback; a standalone pure-C client
does not have that callback. This framing must not be sent to a guessed server.
Native source REA parent is
`ev_d9373a110e6d8f723a4c2ccf46fc3431ba041c63e3bad0c1e3b91818566661ca`;
see `api-contract-native-rea.json`. Four modules parsed without errors; semantic
coverage hit per-file ceilings and 2373 inventoried files were outside selection.

The Library adapter builds a request `header` with `length`, `skip`, `textFilter`,
`sortOrder.{sortOrder,reverse}`, `filters.filter[]`, `totalCount`, `allPlaylists`,
`fillFolders[]`, optional `folderId`, `numLinkTypesInPlaylists`, optional
`curatedItems`, optional `tagFilter`, and feature-dependent inclusion flags.
Generated responses expose `header`, `entity[]`, `pinnedEntity[]`, `statusCode`,
`error`. Numeric enums and protobuf presence must be taken from module 90879,
not reinterpreted as guessed GraphQL nullability. Folder expansion changes the
adapter's effective offset/length and may fetch preceding rows before slicing.

Temporary, locally sanitizing instrumentation observed decoded Library RPC
responses while ordinary UI navigation ran. The original method was restored.
The retained projection removes all personal strings and binary values.
The remote HTTP URL, headers, token domain, revision semantics and remote error
contract behind this native RPC remain **unresolved**. `API_LIBRARY` is blocked.

## Primary Playlist

Recovered modules 33872/82379 establish native local RPC
`spotify.playlist_esperanto.proto.PlaylistDataService.Get` and `Subscribe`.
Requests contain `uri`, `query` and `policy`; generated response fields include
`status`, `data`, `query`. The desktop adapter's `getMetadata` requests a playlist
with offset 0 and limit 0. `getContents` builds a query with offset/limit and
decoration policies; `getPlaylist` combines metadata and contents. Row IDs,
placeholder types and capabilities are meaningful, not just track URIs.

The local RPC's protobuf data and statuses are observable. Its remote backing
URL/method/headers, revisions, page tokens and remote status unions remain
**unresolved**. The `playlist/v2` host constant alone does not prove this primary
view's endpoint. `playlistSection`, curation and concert documents are secondary
surfaces and must never substitute for it. `API_PLAYLIST` is blocked.

## Completion gate

Successful screen rendering and a decoded native RPC do not prove a standalone
network contract. Required external contracts and error/pagination fixture
coverage are still missing. Issue #21 must remain open until those obligations
are resolved; a green offline CTest run alone does not satisfy its closure rule.
