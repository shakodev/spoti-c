# Native Library and Playlist evidence

The desktop Library is an aggregate assembled by native code. Its
`YourLibraryService.All` / `StreamAll` Esperanto calls and the primary
`PlaylistDataService.Get` / `Subscribe` calls are local IPC contracts. Their
protobuf replies are not JSON replies from a single hidden PathFinder query.
Collection synchronization, the playlist rootlist, playlist contents, metadata,
local cache and filtering contribute to the result. The typed catalog must keep
the local aggregate and these backing HTTP services distinct.

## Artifact identity and REA coverage

[api-contract-native-backends.json](api-contract-native-backends.json) pins the
installed Windows `Spotify.dll`, version **1.3.5.3058**, SHA-256
`4f8a965aa4492c6cbfa304956ea8428f10538c74dfae5eaadf54be08572a897a`.
This is a newer native client than the separately pinned 1.3.4.258 XPUI snapshot.
Version-specific source and runtime claims retain their own artifact identity.

The DLL embeds complete protobuf file descriptors. The exact
`collection2v2.proto` descriptor occupies file bytes `[29496800, 29498007)`,
SHA-256 `f5406cc7fa39b9f8a36e476af577c946e07d9fd706519288f36c57496a3c4a17`;
`playlist_api.proto` occupies `[30093952, 30105174)`, SHA-256
`a6d3a154e30e38d3e7425170b72adf5db2ff23d7d5d364eaecb60f3c33008ee6`.

REA/Ghidra 12.1.4 reads verify these exact original bytes:

| Observation | Evidence ID |
| --- | --- |
| Collection descriptor | `ev_34ca606b7c6e2370a0e61a70289f79d7de102651793ef47f8903b51e92be04c5` |
| Playlist descriptor | `ev_5fd01c5c2403dde60be3b7a1a0e2e0fb9c5cb3a3f5cec015036eec6e35bf27b7` |
| Playlist sync reason, `/changes`, `/diff`, `handlesContent`, `decorate` literals | `ev_da615eb1da755d9a113f59279f0fcdd8f8d24dc23e18affd57985515e46d625a` |
| Collection write/paging/delta/initialized literals | `ev_c6d5591266b530152b0c8e11a3d54493097b074606d2d29a7ab1df3709544e24` |
| `x-accept-list-items` and supported item-type literal | `ev_706af7d0015b98966ca0c70419c58cb87bde9885c372ca212664c64378d16fcc` |

Full-DLL deep analysis exceeded its time bound. These focused reads instead use
an inert analyst-derived PE containing three original read-only data spans at
their original virtual addresses. It has synthetic headers, no executable
sections and no entry point; it is never executed. The manifest retains the
container digest, complete synthetic header, original range mappings and hashes,
exact returned REA bytes and profile digests. `tools/native_api_evidence.py`
compares every returned byte against the unmodified original DLL. Static strings
alone do not establish request construction or network reachability.

The manifest also preserves the exact current descriptor dependency closure:
`collection2v2.proto`, `collection_itemdata.proto`, protobuf timestamp/duration,
`save_source.proto`, `playlist_api.proto`, `lens-model.proto`, `signal-model.proto`,
`sorts.proto`, `tags.proto` and `playlist_permission.proto`. All eleven descriptors
resolve in an independent protobuf descriptor pool. Original ranges, bytes,
digests and import edges permit reconstruction without the installed application.
The dependency descriptors have source-byte verification; the two primary
descriptors additionally have the complete REA reads above.

## Backing transport observations

The separately retained sanitized runtime fixtures establish the backing HTTP
requests. The observed regional authority is
`https://gew4-spclient.spotify.com`; this observation does not imply that a
regional host is globally fixed. Credential values, account identifiers,
playlist identifiers, revisions, cursors and item strings are removed before
capture data is saved or supplied to REA.
REA capture parent is
`ev_3b517bbb90ae79bbfb75c58b2c3ef72a64f9e9ed378ade77061d513fc56a4ee7`;
see `api-contract-native-http-rea.json`. Its27 records are bounded decoded
projections from the exact current descriptor closure, rather than original
protobuf bodies. The original entity digests and lengths are retained separately.
The fourteen `observed-backend-http` fixtures have no primary RPC coverage bits.

Native requests carry `authorization` and `client-token` header names, with
`App-Platform: Win32_x86_64`, `Spotify-App-Version: 130503058` and the session
locale (`en-GB` in this capture). The presence of authentication headers is
observed; their values are never retained. Browser-managed headers such as
origin, fetch metadata and content length are not independent token domains.

### Collection synchronization

`POST /collection/v2/paging` uses
`Accept: application/vnd.collection-v2.spotify.proto` and the same Content-Type.
The body is `spotify.collection.proto.v2.PageRequest`, not JSON:

| Tag | Field | Current type |
| --- | --- | --- |
| 1 | `username` | string |
| 2 | `set` | string |
| 3 | `pagination_token` | string |
| 4 | `limit` | int32 |

Proto3 scalar omission denotes its default, rather than JSON null. The initial
request omits an empty pagination token. The observed `set` is `collection`.
The read-only controlled first page uses limit 2. `PageResponse.items` is repeated
`CollectionItem` tag 1; `next_page_token` is string tag 2 and `sync_token` is string
tag 3. A nonempty next-page token is opaque and is copied into the next request;
it is not an integer offset. Both first and continuation pages returned HTTP 200
with two items in the observed nonempty `collection` set. The descriptor also
defines a sync token for synchronization state. An intentionally invalid cursor
returned HTTP400. Its empty body establishes no structured protobuf error
message.

`POST /collection/v2/delta` was independently observed with the same media type.
`DeltaRequest` contains username tag 1, set tag 2 and last-sync-token tag 3.
`DeltaResponse` contains delta-update-possible bool tag 1, repeated items tag 2
and sync-token tag 3. The response can contain no changed items. The initialized
and write paths also occur as native literals and descriptor declarations; this
read-only investigation sends no collection writes and claims no write contract
acceptance.

### Playlist and rootlist

The primary playlist contents request uses
`GET /playlist/v2/playlist/{base62PlaylistId}`. Rootlist contents use
`GET /playlist/v2/user/{username}/rootlist`. These routes return protobuf
`spotify.playlist4.proto.SelectedListContent`. Neither route is the unrelated
PathFinder `playlistSection` query.

Observed pagination query parameters are `from` and `length`; `decorate` is a
comma-separated selection such as
`revision,attributes,length,owner,capabilities,status_code`. The ordinary client
also requests rootlist timestamp decoration. A controlled playlist first page
with `from=0&length=2` returned `length=2`, `contents.pos=0` and two items. The
continuation `from=2&length=2` returned `contents.pos=2` with no items: an observed
exhaustion boundary. A negative offset returned HTTP 200 with length zero and
metadata only; it must not be labeled a transport-error fixture.
An independent missing-playlist read returned HTTP503 with an empty body; this
is retained as an observed transport failure, not renamed NotFound. Rootlist
first/next page reads were separately captured, and a nonnumeric `length`
returned HTTP400 with no structured error body. Neither case establishes all
possible error statuses or permission/market behavior for every account.

Native incremental reads append `/diff`, send opaque `revision` plus
`handlesContent`, and can return selected content or an HTTP 304 no-change
result. The static `/changes` literal does not authorize or validate mutation.
Additional observed header names include `spotify-playlist-sync-reason`,
`x-accept-list-items`, `spotify-accept-geoblock` and lens-related headers. Only
their names are retained when their values are not on the public-value allowlist.
No invented value should be required by the catalog from these observations.

`SelectedListContent` has optional revision bytes tag 1, length int32 tag 2,
attributes tag 3, contents tag 5, diff tag 6, sync-result tag 7, owner username
tag 16, capabilities tag 18 and applied lenses tag 22. Metadata-only responses
may omit contents. `ListItems` requires position int32 tag 1 and truncated bool
tag 2; repeated `Item` is tag 3 and continuation-token string is tag 6.
`Item.uri` is required string tag 1; attributes are optional message tag 2.
Opaque revisions are binary identifiers, not UTF-8 strings or offsets. Diff
fields and operation variants are separately preserved in the current descriptor
manifest; receiving a diff does not mean the client can use it as a full snapshot.

## Schema drift, bounds and retry policy

The historical librespot protobuf files are useful comparisons, not current
wire authority. Current `CollectionItem.added_at` tag 2 is **int64**, while the
historical schema says int32. Current collection items also have optional
`item_data` tag 5. Playlist additions include `ListItems.lens_removed_items` tag 7,
`SelectedListContent.signal_results` tag 23, more attribute fields and an
available-signals diff field. The manifest records every detected top-level field
difference. A decoder generated from the historical schema can discard unknown
fields and truncate timestamps; capture must use the exact current descriptor
closure when claiming those values or fields.

Future pure-C transport decoding should use these verified tags with bounded
nanopb callbacks/static limits, preserve 64-bit timestamps, reject missing proto2
required fields, skip well-formed unknown fields, and treat byte cursors/revisions
as opaque. The catalog's 16-KiB request and 256-KiB response limits are local
implementation bounds, not claims about maximum server payloads. Exceeding a
bound must fail explicitly rather than truncate a protobuf or silently drop a
page. Captured replies are projections for fixture verification, not arbitrary
content playback or library mutation permission.

The capture establishes success, continuation/exhaustion and the specified
negative cases. It does not establish server-wide rate-limit, token-expiry or
backoff behavior. Therefore catalog clients should expose status, distinguish
local IPC failures from HTTP failures and avoid automatic retry loops. Repeated
invalid cursors and malformed inputs are not retryable; an authentication failure
belongs to the appropriate token manager. Any future retry policy requires its
own evidence and bounded cancellation behavior.
