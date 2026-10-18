# PathFinder request contract (issue #21)

## Provenance

Spotify desktop 1.3.4.258. The extracted XPUI directory is
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
The installed `v8_context_snapshot.bin` is
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

## Verification and remaining work

2026-10-18: Debug configured with `ENABLE_ASAN=ON`; build passed with MSVC C17,
`/W4 /WX /fsanitize=address`. Full `ctest --test-dir build --output-on-failure`:
37/37 passed. The new `api_contract` C test verifies all eleven catalog entries,
the proof gate, body limits, enum/field failures, exact fixture hashes/counts,
request headers/variable types and enabled Album success/NotFound/exhaustion.
Fifteen sanitized fixtures are committed. CNG handle accounting is balanced;
JSON uses caller-owned test pools without allocator fallback. No request is
performed by the contract module or default tests.

Only primary Album is enabled. Home/section, legacy/list/category Search,
AlbumTracks, Artist and Lyrics still have explicit missing-proof masks. Library
and primary Playlist additionally lack a proven standalone network contract
behind the observed native RPC. These are unresolved issue requirements, not
hardware-dependent skips or successful acceptance claims. Do not close #21.

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
