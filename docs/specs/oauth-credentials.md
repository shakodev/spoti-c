# OAuth PKCE, credential storage and AP authentication

Contract recorded before implementation for issue #10, 2026-10-18.

## Evidence and boundaries

Official contracts: [PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow),
[refresh](https://developer.spotify.com/documentation/web-api/tutorials/refreshing-tokens),
[redirects](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri).
GET `https://accounts.spotify.com/authorize`: `response_type=code`, registered
`client_id`, exact `redirect_uri`, requested space-separated `scope`, random
`state`, `code_challenge_method=S256`, base64url-without-padding SHA256 challenge.
The verifier is 43..128 unreserved ASCII characters. Generate verifier and state
independently from 32 CNG random bytes each (256 bits).

POST `https://accounts.spotify.com/api/token`, Content-Type
`application/x-www-form-urlencoded`, with `grant_type=authorization_code`,
`client_id`, `code`, identical `redirect_uri`, and `code_verifier`. Refresh uses
`grant_type=refresh_token`, `client_id`, and `refresh_token`. No secret or Basic
header. Require HTTP 200, `token_type=Bearer`, nonempty `access_token`, positive
integer `expires_in`; initial issuance requires `refresh_token`. Refresh replaces
the old refresh token only when a valid new one is supplied. Optional `scope`
records the grant; successful OAuth does not prove private-service acceptance.
Redirects/retries are disabled for these POSTs. Errors never install partial
credentials. Access token expiry uses the monotonic clock.

REA `analyze_javascript_application` inspected `ref/xpui/home-ads-container.js`
and `xpui-debug-window-contents.js` as inert text. Parent Evidence
`ev_3090e16ead5c0a1c9e1ad0162e1fe2e7dd996b04cedf7d29d0d5d64a75477007`,
summary `ev_82e88f63ab96b4202f3e10c3c68fa4b2dcde35f435b059b49ae431362485f5f9`;
artifact SHA256 `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Two files parsed, no parse failures; 269 files unselected, semantic node ceiling
2000 (partial/truncated). Existing [XPUI evidence](xpui-static.md) establishes
token consumption/expiry, not PKCE issuance or private scopes. No internal HTTP
endpoint is introduced here: issuance follows the public documented contract.
Login5/clienttoken/PathFinder remain outside this issue and their evidence gates
remain in force. No query hashes apply to this public OAuth endpoint.

Pinned librespot `19959512c212cbc48a3ff64382fe204f52c659a7`:
`oauth/src/lib.rs:60,313` supplies endpoints and PKCE; its discarded CSRF state
must not be copied. `core/src/authentication.rs:61` creates type 3 token auth.
`core/src/connection/mod.rs:108` encodes ClientResponseEncrypted, then takes
canonical username and reusable type/blob from APWelcome. `session.rs:163-201`
opens a second connection with the returned credentials after token auth.
`protocol/proto/authentication.proto` defines the wire fields and enum values.
Only returned stored types 1/2 are accepted for persistence; never password or
access-token types. A rejected AP login is explicit compatibility failure.

## Ownership and limits

One network owner drives the flow, HTTP and AP. OAuthFlow is allocated once
inside a caller-owned secret arena <=256 KiB, phase-shared with protocol scratch;
dispose it before constructing AP in that same arena. No background thread or
polling timer. Wait on socket/HTTP events and the nearest deadline. Verifier,
state, code, request and response buffers have fixed bounds and are wiped when
retired. Cancellation retains HTTP-borrowed buffers until final consume. Shutdown
must finish before arena reuse. Browser launch is explicit and uses ShellExecute.

IPv4 loopback only, exclusive bind to 127.0.0.1. Registered fixed ports are kept;
a registered URI without a port selects an ephemeral port as public docs permit.
`localhost`, non-loopback hosts, userinfo, fragments, queries and ambiguous paths
are rejected. Callback requires GET, matching Host and exact path, <=4096-byte
complete headers, strict percent decoding, exactly one matching state and exactly
one code or error. Reject duplicate parameters, NUL/control bytes, bodies and
replays. Wrong requests leave admission open until deadline. One accepted peer
has a five-second deadline. A valid callback is consumed once. Default overall
deadline is three minutes, maximum five. Neither browser responses nor logs echo
codes, tokens or state. Only explicit loopback fixture endpoints may override
the token destination in offline tests.

CredentialRecord v1 separately stores OAuth client ID, refresh token (<=2048),
canonical username (<=1023), returned AP type/blob (<=1024), and random stable
40-hex device ID. Explicit little-endian lengths avoid ABI/padding dependence.
Bound plaintext to 8 KiB and encrypted file to 16 KiB. CurrentUser DPAPI with
application entropy and UI forbidden protects the complete record. A protected
owner/System DACL is applied to the exclusive temporary file; flush and atomic
MoveFileEx replacement precede success. No plaintext temporary file. Logout
deletes the file and wipes the in-memory record; deletion failures are reported.
DPAPI and security-descriptor allocations are zeroed/freed and accounted, as are
native handles. Corrupt, foreign-context, unsupported-version and oversized files
fail closed with a zero output record. No access token field exists on disk.

## Verification

Default CTest is offline: RFC 7636 challenge, state/decoding/replay/mismatch,
cancel/expiry, fragmented loopback and bounded input, real WinHTTP fixture token
exchange/rotation/errors/cancellation, real DPAPI round trips/tampering/foreign
entropy, atomic replacement/logout, and encrypted AP token-to-stored reconnect,
canonical changes and rejection. Explicit resource ledgers supplement ASan.
`ENABLE_LIVE_OAUTH_TEST` registers the account-dependent browser/AP compatibility
check only when enabled. Requires configured developer client/redirect/scopes;
no built-in client identity and no presumed private-scope equivalence. A skipped
live test provides no evidence of service acceptance.

## Module integration and live check

Initialize/load a CredentialRecord for the configured OAuth client ID. Never use
a refresh token with a different client ID. Start OAuthFlow in the secret arena,
call `oauth_begin(flow, true)`, and wait on `oauth_wait_handles` with
`oauth_next_wait_ms` before `oauth_poll`. At OAUTH_CODE call `oauth_exchange` with
the existing HTTP client; its exact header-origin allowlist must contain
`https://accounts.spotify.com`. At OAUTH_DONE, `oauth_take_token` transfers the
memory-only access token and updates the record's refresh token. Persist the
record with `credential_store_save`; report any persistence failure to the owner.
Refresh uses a fresh flow plus `oauth_refresh` and the same one-use result path.

Finish `oauth_shutdown` (including pending HTTP retirement) before reclaiming
its arena. Initialize ApAuth with the record and access token, wipe the caller's
token, and attach ApConfig.auth before ap_start. Drive the existing AP event API.
The controller sends login when framing is ready; token welcome updates the
record and initiates the second handshake. AUTHENTICATED is exposed only after
stored login succeeds. Save the updated record at that point, including any
canonical-name or reusable-credential rotation. On restart `ap_auth_init` with a
NULL access token selects the persisted returned type/blob directly. Shut down AP
before clearing ApAuth or modifying its borrowed record. Logout cancels active
owners, deletes persisted credentials, and clears memory. No UI is added by this
module; the future application owner performs these explicit transitions.

The live target is compiled and registered only with
`-DENABLE_LIVE_OAUTH_TEST=ON`. In an x64 developer shell with the pinned Python
protobuf environment configured:

```powershell
$env:SPOTIC_OAUTH_CLIENT_ID = '<your registered application ID>'
$env:SPOTIC_OAUTH_REDIRECT_URI = 'http://127.0.0.1/callback'
$env:SPOTIC_OAUTH_SCOPES = 'streaming user-read-private'
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_LIVE_OAUTH_TEST=ON
cmake --build build-rel
ctest --test-dir build-rel -R '^api_oauth_live$' --output-on-failure
```

The example redirect must actually be registered for that application. The test
opens the system browser only when all three variables are configured; otherwise
it returns CTest skip code 77. It stores no account data on disk and prints no
tokens or account identifiers. Success establishes OAuth issuance and AP
token/stored login compatibility for that account/client at that time; it does
not establish Login5/clienttoken/private HTTP scope acceptance. The AP client
version is the pinned librespot `core/src/version.rs:20` value 124200290.
