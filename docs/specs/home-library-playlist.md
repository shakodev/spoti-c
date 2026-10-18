# Home, Library and primary Playlist contracts

Evidence/source identities and shared headers are defined in `pathfinder.md`.
HTTP documents are pinned to desktop 1.3.4.258 and replayed through 1.3.5.3058.
Native contracts below use the separately identified 1.3.5.3058 snapshot.
These observations do not establish Web Player compatibility.

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

The root request is not paginated. Modules 26662/83314 use an ordinary single
query with no offset/cursor; `sectionItemsLimit` bounds each embedded section.
Its positive and independently observed HTTP400 for a boolean
`sectionItemsLimit` pass the fixture gate. The latter is labeled rejected input,
and does not weaken the normal unsigned-integer schema. A source-backed
nonpagination record replaces an inapplicable root-page fixture.

`homeSection` and `homePinnedSections` use the same hash in module 26662; they are
distinct operation names, not aliases whose response can be assumed identical.
`xpui/home-v2-section.js` supplies `uri`, `homeEndUserIntegration`, `timeZone`,
`sp_t`, `sectionItemsOffset`, `sectionItemsLimit` (default 20), and
`includeEpisodeContentRatingsV2`. It checks `data.homeSections.__typename ==
HomeSectionCollection` before advancing the item offset. Pinned Home supplies
`uris[]` and `sectionItemsLimit` (default 10). Pinned Home remains a separate
operation outside the enabled catalog; its response must not be inferred from
HomeSection.

Independent replay establishes `data.homeSections.sections[0]` with
`__typename: HomeSection` and `sectionItems.{items,pagingInfo}`. Missing sections
return a nested `NotFound`; the consumer also handles `GenericError`. An initial
request with limit2 returned20 items and `nextOffset:20`; offset2 repeated that
initial page. A subsequent captured request uses the returned offset20 and
receives20 items, but its `nextOffset` is still20. This proves the request/response
contract and a nonadvancing continuation boundary, not working full-scroll
pagination. A caller must stop on a repeated/nonadvancing cursor and report that
boundary; it must never keep requesting20 indefinitely. The manifest counts
only a positive offset previously returned by the service as pagination
evidence. HomeSection now passes its fixture gate. Error fixtures separately
retain the nested NotFound and rejected boolean-limit HTTP400. Full Home item
unions remain discriminated data; unsupported members require an explicit result.

## Library

Recovered 1.3.5 modules 20609/5358/76148 establish a **native local RPC**:
`spotify.your_library_esperanto.proto.YourLibraryService`, `All` (single response)
or `StreamAll` (streamed). The generated client protobuf-encodes its request and
decodes its response; module 76148 forwards it through `executeEsperantoCall`.
This is not an HTTP request and is not a PathFinder document.

The native framing uses three length-prefixed byte strings: service name,
method name, protobuf payload. `DataView.setInt32` is used without a little-endian
flag. JavaScript/CEF supplies the native callback; a standalone pure-C client
does not have that callback. This framing must not be sent to a guessed server.
The native snapshot SHA-256 is
`aefd55fa7f292f438ee7cec4ed4803a2b7ff6b7dc6a8453ec0e32b151d2b07df`.
Native source REA parent is
`ev_588525847e8c10120ab396ab3a47029259f9e9a4487c761eb56ea3f5040a677e`;
see `api-contract-current-native-rea.json`. Three selected modules parsed without
errors; semantic coverage is partial and bounded per file. Bridge76148 is covered
separately by transport parent
`ev_92e2280027f1c9021563045044dfedbad8b289c40f207a80f4c77b08975b0014`.
The earlier 1.3.4 source and projections remain separately identified historical
evidence.

The Library adapter builds a request `header` with `length`, `skip`, `textFilter`,
`sortOrder.{sortOrder,reverse}`, `filters.filter[]`, `totalCount`, `allPlaylists`,
`fillFolders[]`, optional `folderId`, `numLinkTypesInPlaylists`, optional
`curatedItems`, optional `tagFilter`, and feature-dependent inclusion flags.
Generated responses expose `header`, `entity[]`, `pinnedEntity[]`, `statusCode`,
`error`. Numeric enums and protobuf presence must be taken from module20609,
not reinterpreted as guessed GraphQL nullability. `folderId` and `fillFolders`
are int64 values, not Spotify URI strings. Folder expansion changes the
adapter's effective offset/length and may fetch preceding rows before slicing.

Locally sanitizing instrumentation observed independent `All` requests at skip0
and skip2 with length2, each returning two entities and status200. A nonexistent
int64 folder ID returned status404 and a nonempty error; skip999999 returned an
empty entity array with totalCount22. Unsupported filter/sort values sometimes
return200, so they are not classified as error evidence. The generated response
also includes independent pinned entities and loading/filter metadata. The
catalog requires the adapter's `header` object; `updateThrottling` is optional.
Proto3 omitted scalars use defaults, not JSON null. The retained decoded fixture
projection materializes generated-client defaults and redacts int64/string
identifiers; it is not the original protobuf byte stream.

There is no HTTP URL, HTTP method/header set, persisted hash or independently
acquired token at this local boundary. The exact RPC method is `All`; session
authorization belongs to the host. `callSingle` propagates native failures and
cancels through the native handle when its AbortSignal fires. It does not add an
RPC retry loop. `StreamAll` is a distinct subscription, outside this single-call
catalog entry. Callers must keep cancellation, streaming updates and loading
state distinct from a completed page.

The backing Collection paging/delta and Rootlist snapshot/diff HTTP contracts,
their authentication headers, current protobuf schemas, revisions and actual
error/page fixtures are specified in [native-library-playlist.md](native-library-playlist.md).
The primary Library combines those synchronized sources with metadata, local
state, sorting, filtering and folder expansion. One Collection page is not the
primary Library response. `API_LIBRARY` now describes the proven local contract;
implementing its pure-C service remains a separate feature task.

## Primary Playlist

Recovered 1.3.5 modules 20240/5358 establish native local RPC
`spotify.playlist_esperanto.proto.PlaylistDataService.Get` and `Subscribe`.
Requests contain `uri`, `query` and `policy`; generated response fields include
`status`, `data`, `query`. The desktop adapter's `getMetadata` requests a playlist
with offset 0 and limit 0. `getContents` builds a query with offset/limit and
decoration policies; `getPlaylist` combines metadata and contents. Row IDs,
placeholder types and capabilities are meaningful, not just track URIs.

The exact single-call binding is service
`spotify.playlist_esperanto.proto.PlaylistDataService`, method `Get`. Its `query`
contains `range.{start,length}`, sorting, filtering and placeholder controls;
the public adapter maps offset/limit to that range. Optional `policy` selects
metadata, row identifiers, state and extension decorations. An omitted policy
can yield rows with default-valued decorations, rather than fully hydrated
tracks. Source module20240 specifies the generated message fields and numeric
enums; module5358 specifies the primary adapter's policy composition.

Independent Get calls at range start0 and start2, length2, each returned two
`data.item` rows and `status.statusCode:200`. Malformed playlist URIs returned
400. Native statuses403/451 take the adapter's nonviewable metadata path.
Positive responses carry `data.{item,playlist,unfilteredLength,unrangedLength}`
and other policy-selected state; error replies may omit data. Row IDs,
placeholder types and capability flags must remain discriminated fields.
Authorization and cancellation use the same local session/bridge boundary as
Library; `Subscribe` is a separate streaming operation.

Actual backing requests now independently establish the primary
`GET /playlist/v2/playlist/{id}` protobuf snapshot and `/diff` revision reads,
including page/exhaustion and HTTP503 missing-item observations. The backing
spec preserves the transport status without inventing a NotFound response.
`playlistSection`, curation and concert documents remain separate surfaces.
`API_PLAYLIST` describes the proven local Get contract, not a GraphQL alias.

## Completion gate

All required primary contracts now have source identities, typed fields and
independent positive/error/page evidence, or an audited nonpagination rule.
Backing network contracts have separate native byte and sanitized HTTP evidence.
`contract_lookup` returns descriptive metadata only. For native RPC, HTTP URL
and header proofs are inapplicable and excluded from its required-proof mask;
they are not falsely marked proven. A caller must branch on transport and
implement the service/bridge separately. No endpoint is executed by this module.
