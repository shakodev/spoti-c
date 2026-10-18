# PathFinder request contract (issue #21)

## Provenance

Spotify desktop 1.3.4.258. The extracted XPUI directory is
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
The initially inspected `v8_context_snapshot.bin` is
`c20d9408009a281ee4152b63077a8309eb17ec172bbf80b80dc7879d586bb099`.
Its UTF-16LE source span `[587581,17075529)` decodes to `xpui-modules.js`,
UTF-8 SHA-256 `a953e49187f4fbd95aadf905d9a421f01b9b85b4d01cd5b679bd1547dad3f2e4`.
`tools/extract_xpui_snapshot.py` verifies the pinned snapshot before extraction;
`tools/split_xpui_snapshot.mjs` parses it without execution and wraps exact AST
member spans in inert Rspack registrations for bounded REA analysis.
`api-contract-sources.json` verifies eighteen selected module spans against those
exact original bytes and records offsets plus original/derived hashes.

REA parent `ev_1598043638d84cda7402b5b188cf254b6ab9c1a85e83d6a54351050338a8f2e3`
analyzed fourteen selected derived modules (zero parse failures). See
`api-contract-snapshot-rea.json`. Analysis is partial: 2363 other inventoried
files were outside scope. The derived wrappers are analyst artifacts, not the
original snapshot or evidence that a factory executed.

The locally sanitized catalog HAR was inspected by REA parent
`ev_ff9755febc8df58837964874cc73b798bb7eb13f77f88a6c1f08cc3c8ccc6936`.
The local capture and REA scenario observations corroborate request assembly;
REA's historical HAR adapter itself does not prove runtime attribution.
Boundary/lyrics HAR parent:
`ev_1c410a64c8e0fb46249c4bc5c98f05051a7f89c3b42e22693128ecbe95e6399c`;
main Search HAR parent:
`ev_13c2123030f2e913fc4350e083c510395a71cef43ea686678e4693a40e8fb07a`.
All three adapter summaries are retained in `api-contract-capture-rea.json`.

The installed client updated to 1.3.5.3058 before the follow-up capture. The
follow-up deliberately replayed the **unchanged 1.3.4.258 document names,
hashes and variable shapes** through that client's authenticated transport.
The primary Home and Album documents in the newer client have changed; their
new hashes are not substituted into this pinned catalog. The sanitized replay
HAR SHA-256 is
`b1d51bc6dddd31bfdd64e89bbee462a03dc650c73d5caad5435741bccd82561c`,
REA parent
`ev_715bacdcb515b002bdc22a20a2583877dba8793b2af80aae233ef658811e2382`;
see `api-contract-replay-rea.json`. Successful replay establishes acceptance of
the pinned request at capture time. It does not claim that the newer client's
normal screen route uses that document. Runtime global app-version headers in
this replay identify 1.3.5.3058; the older source-local Album/Artist override
remains part of the pinned call description.

The final HTTP HAR adds independent SearchDesktop success/page/error requests
and HomeSection's actual returned offset20. Its SHA-256 is
`a8647297e6eeb5b8a2eb6fa6954858bef1afcd9d8fe7485732db6dd28894aeaf`,
REA parent
`ev_5773c3cceb8dae873068fc9c84c53b1d91a4a06c3ffb527861936c1d26196baf`;
see `api-contract-final-http-rea.json`. The HomeSection response repeats cursor20;
the contract documentation requires callers to stop at that nonadvancing boundary.

## Request assembly

Modules 62037, 32836, 6854, 69677 and 95096 establish:

* External query URL: `https://api-partner.spotify.com/pathfinder/v2/query`.
* Method: POST. JSON body: `operationName` (document name), `variables` (object),
  `extensions.persistedQuery.{version:1,sha256Hash}`. No GraphQL SDL is recovered.
* `Accept: application/json`, `Content-Type: application/json;charset=UTF-8`.
* `Authorization: Bearer <access token>`, supplied by the authenticated desktop
  transport. Never persist the value in a fixture or log.
* Global desktop headers observed: `App-Platform: Win32_x86_64`,
  `Spotify-App-Version: 1.3.4.258`, `Accept-Language: en-GB` (session locale).
  Album/Artist callers additionally supply lowercase `spotify-app-version:
  896000000`; preserve that source-local override rather than applying it globally.
* `.withoutMarket()` suppresses the builder's implicit `market` query argument.
  Explicit parameters remain URL query parameters; JSON variables stay in the body.
* No `client-token` header was observed on these desktop PathFinder requests.
  Do not infer Web Player token requirements from the desktop branch.

The bearer belongs to this service origin. Existing token-manager domain and
origin checks still apply. Do not send AP session keys, OAuth refresh tokens,
client-token secrets or a bearer authorized only for another origin.

## Errors, fallback and retries

Module 32836 falls back to `/pathfinder/v1/query` on HTTP 405/412 or a response
whose `errors` includes `message == PersistedQueryNotFound`. Fallback requires
the document's `value` (full query text). The inspected documents have `value:
null`, so the client throws GraphQLFallbackUnavailableError; this client must
report unavailable, never invent SDL or retry the same missing query forever.

Module 90185 rejects GraphQL `errors` even alongside partial data and normalizes
an ordinary Error into a message array. Consumer adapters also check missing
data and response union discriminants. Unknown union members must produce an
explicit unsupported result. Authentication refresh, cancellation and bounded
429/5xx scheduling belong to the existing HTTP/token layers. No source evidence
here establishes unlimited retries, a universal Retry-After value, or permission
to switch a private contract to the public Web API.

## Evidence gate and memory policy

`api_contract.h` describes operations without performing requests. It has fixed
field tables, provenance, required proof fields and finite body caps; no heap,
I/O, timers or global mutable state. A hash alone is insufficient to enable a
contract. Lookup must fail closed while request/response, token/retry or fixture
coverage is unresolved. Limits are local admission policies, not claims about a
server maximum; oversized replies are explicit failures and never silently grow.

Captured fixture strings are redacted locally before storage or REA inspection.
Arrays, keys, scalar types, nullability and discriminants are preserved. Derived
error/pagination test mutations must be labeled as such and cannot masquerade as
observed service responses. Credentials and original account content are excluded.

## Verification

2026-10-18: Debug configured with `ENABLE_ASAN=ON`; build passed with MSVC C17,
`/W4 /WX /fsanitize=address`. Full `ctest --test-dir build --output-on-failure`:
37/37 passed. The new `api_contract` C test verifies all eleven catalog entries,
the proof gate, body limits, enum/field failures, exact fixture hashes/counts,
request headers/variable types, transport-specific RPC identities and independent
success, error and continuation/exhaustion cases.
CNG handle accounting is balanced;
JSON uses caller-owned test pools without allocator fallback. No request is
performed by the contract module or default tests.

The follow-up enables Home root/section, SearchDesktop, SearchTracks,
SearchTopResultsList, Album, AlbumTracks, Artist overview and Lyrics.
Home root, Artist overview and Lyrics
have source-backed nonpagination records in the manifest: their nested related
operations are not continuation pages of these requests. Every enabled entry
has its own observed positive and service error/rejected-input fixture. Search
errors at numeric limit 101 preserve the normal variable types; boolean limits
are separately labeled `observed-rejected-input` and name the single rejected
field. They test service rejection and never redefine the normal schema.
Synthetic fault injections remain separate and cannot supply observed coverage.

Library and primary Playlist now have proven native RPC descriptors. Their
HTTP URL/header proofs are excluded as inapplicable at that boundary. Lookup is
metadata-only and never implies that the program has Spotify's native bridge.
The separately observed Collection/Rootlist/Playlist backing protocols and
current native protobuf descriptors are specified in
[native-library-playlist.md](native-library-playlist.md). The catalog's eleven
operations and 53 bounded fixtures are complete; supporting backend projections
cannot substitute for the primary RPC fixtures or contribute coverage bits.

Extraction can be reproduced with `tools/extract_xpui_snapshot.py SNAPSHOT
NEW_DIRECTORY`, followed by `tools/split_xpui_snapshot.mjs SOURCE.js NEW_MODULES
BABEL_PARSER_PATH` and `tools/api_contract_sources.py --help` for original-byte
verification. Capture is explicitly opt-in: `node tools/capture_api_contracts.mjs
http://127.0.0.1:PORT OUTPUT.har SECONDS` observes the approved official-client
debug session while the operator navigates. It never launches a client or
changes system proxy/certificate configuration. Reinspect new sanitized captures
with REA; the fixture publisher rejects capture digests absent from the supplied
REA summary manifest. Temporary bridge instrumentation was restored and the
task-owned debugging process/listener was closed after capture.

`tools/api_contract_fixtures.py` accepts repeated `--capture` and `--evidence`
arguments. Each exact sanitized HAR digest must occur in a supplied REA summary;
neither an operation-name match nor a historical source hash authenticates an
uninspected capture. The C checker independently checks exact fixture bytes,
operation identity, request types/headers, response status/unions and counts.

`tools/api_contract_native_fixtures.py` merges the restored, locally sanitized
native bridge capture. Its Evidence ID identifies static source, not an REA
runtime transaction. `tools/native_api_capture.py` converts already sanitized
current-schema protobuf observations to an explicitly labeled HAR projection;
`tools/api_contract_backend_fixtures.py` checks its REA digest and exact decoder
closure before publishing supporting fixtures. Projection timestamps use a
Unix-epoch unknown-time sentinel. Original entity digests and byte counts remain
separate from decoded JSON hashes; no raw credentials, IDs, lyrics or binary
field values are saved in fixtures.

The 1.3.5 native snapshot source is SHA-256
`aefd55fa7f292f438ee7cec4ed4803a2b7ff6b7dc6a8453ec0e32b151d2b07df`.
Its inert source span is `[589917,17343559)` in UTF-16LE, decoded UTF-8 SHA-256
`991cc95a5fdb9104aba8aa95c3de511c577e914be155447ff12cabc05715e296`.
The splitter found2422 factory modules. The selected native factories are20609
(Library),20240 (Playlist) and5358 (adapters); their REA parent is
`ev_588525847e8c10120ab396ab3a47029259f9e9a4487c761eb56ea3f5040a677e`.
Bridge76148 and current transport factories68213/92464/85418/41332/83873 were
independently inspected under parent
`ev_92e2280027f1c9021563045044dfedbad8b289c40f207a80f4c77b08975b0014`.
No unselected-module or partial semantic graph is claimed complete.
`api-contract-current-sources.json` verifies the exact original UTF-16 byte
spans and derived hashes of these nine factories against the pinned snapshot.

Temporary instrumentation was restored. The task-owned Spotify process and
loopback proxy/debug listeners were closed, and temporary CA/key files removed.
Process-scoped capture changed no Windows trust or proxy settings.
