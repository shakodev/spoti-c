# Internal authentication tokens

Issue #11 investigation, 2026-10-18. The native protobuf request-assembly gate
passed before TokenManager implementation. No live service acceptance is claimed.

## Evidence inventory

* Installed Spotify 1.3.4.258 `Spotify.dll`: SHA-256
  `ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde`.
  REA opened this exact x86-64 PE through Ghidra 12.1.4. The broad analysis passes
  timed out; a byte-inspection profile completed with automatic code/reference
  analyzers disabled. Its limitations do not establish absence of native code.
* Supplemental byte inspection found the clienttoken URL at file offset
  29413840, `application/x-protobuf` at 29413888, the Login5 v3 URL at 29437048,
  and its nearby protobuf media type at 29437088. Nearby diagnostics mention
  clienttoken protobuf parsing, unsupported operations, and Login5 hashcash.
  Literals establish candidates; they do not alone establish request assembly.
  Focused Microsoft DUMPBIN 14.51.36252 disassembly subsequently located the
  clienttoken builder at VA `0x181305dcc..0x18130606a` and Login5 v3 builder at
  `0x181340da8..0x1813410d1`. Both load the static POST string at `0x18265fa88`,
  their endpoint literal, Content-Type application/x-protobuf, and Cache-Control
  `no-cache, no-store, max-age=0`. Clienttoken additionally sets Accept
  application/x-protobuf. Login5's explicit Accept header is a librespot
  observation, not present in this native builder. Header names are at
  `0x181c5be20`, `0x181c5bd8c`, `0x181c5bdc8`; cache policy at `0x181b3cf58`.
  Builder helpers are method `0x1815701a8`, URL `0x181570824`, header insertion
  `0x18156ffec`, and body `0x1815703f0`. Native client-token insertion occurs in
  `0x18145dbbc..0x18145dcf0` (literal `0x181c321b8`); another native header path
  references `0x18198d468` from `0x1800b68b0..0x1800b6a1c`. These are bounded
  static instruction observations, confirmed against REA's original-DLL bytes.
* The native DLL embeds FileDescriptorProto bytes for
  `spotify/client_token/v0/client_token_http.proto` at file offset 29420816
  (3213 bytes) and `connectivity.proto` at 29416064 (3983 bytes). Independent
  Python protobuf parsing confirmed ClientTokenRequest tags 1/2/3,
  ClientDataRequest version/client-ID/connectivity tags 1/2/3, response
  discriminant/grant/challenges tags 1/2/3, grant token/expiry/refresh/domains
  tags 1/2/3/4, and hashcash length/hex-prefix tags 1/2. The native schema also
  has derived-value request tags 10/11, current-token tag 5 and derived-value
  version tag 10, absent from the pinned reference; they are not fabricated.
  Native Windows tags clarify the reference's unknown names: 1 major version,
  2 minor, 3 build, 4 platform ID, 5 processor architecture, 6 native processor
  architecture, 7 process machine, 8 native machine, 9 server flag, 10 Windows
  Store flag, 11/12 S/X mode, 13 Server R2 build, 14 Wine version. A desktop
  client must not copy the reference's unconditional field-10 true value and
  thereby claim to be a Store application. Login5's native type/field names are
  present as lite-protobuf metadata around offset 29438720, without a full
  FileDescriptorProto. Its exact wire tags remain separately attributed to the
  pinned protocol definitions until corroborated.
* XPUI directory SHA-256
  `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
  Existing REA parent Evidence
  `ev_3090e16ead5c0a1c9e1ad0162e1fe2e7dd996b04cedf7d29d0d5d64a75477007`
  analyzed `home-ads-container.js` and `xpui-debug-window-contents.js`: two files,
  no parse failures, 269 unselected files, partial semantic graph at a 2000-node
  ceiling. `home-ads-container.js` byte offset 12126 calls an injected getToken
  and formats tokenType plus accessToken. This is token consumption, not JSON
  issuance. `pip-mini-player-snapshot.js` registers AuthorizationAPI;
  `xpui-snapshot.js` delegates to externally supplied modules. Do not relabel
  librespot protobuf observations as an XPUI JSON contract.
* Pinned librespot source revision
  `19959512c212cbc48a3ff64382fe204f52c659a7`: `core/src/spclient.rs:146` POSTs
  ClientTokenRequest protobuf to `https://clienttoken.spotify.com/v1/clienttoken`
  with Accept application/x-protobuf; `core/src/login5.rs:61` POSTs LoginRequest
  protobuf to `https://login5.spotify.com/v3/login`, adding client-token. These
  are independent source-reference observations. Native methods, URLs and
  protobuf media types corroborate this path; they do not prove an XPUI JSON
  transport or generic PathFinder headers.

## Completed request gate and portable evidence

Original-DLL REA byte Evidence: clienttoken builder
`ev_5f4703f11807649a94f612b5cdf0f19fc8e18cca68a79327f559d274a0e727c3`,
Login5 builder `ev_822405093b7f245b18b14c19d701b3b26387511bb818d24dad6a029d5e68075c`,
client-token insertion `ev_7d3ebb9bd04b4e3da34f7dd33020ad4d9d016ebe3c95dfbb5b20dda6beb38190`,
clienttoken descriptor `ev_17e06e59f6f74e2a8984ea2d32d47150c0d97f6af9cf766f92a491316fbbb2dd`.
The byte-profile digest is
`bbf5b701809af02de0028254d3f1b9bef8cf061e1cc5a84069704d35eb0315df`.
Portable range hashes, literal Evidence IDs and provenance are retained in
`tests/fixtures/auth/native-evidence.json`.

Bounded decompilation used analyst-derived PE inspection containers made by
`tools/auth_evidence.py`: selected original pages are copied byte-for-byte at
their original VAs; only container headers are synthesized. Missing code/data
and imports remain unknown, and these files must never be executed. Clienttoken
container SHA256 `42c5521382dec2268ee4284a7fc8ca0ff1ea7472daea4d89e8afd55407c4f55b`,
REA dossier `ev_e7e042ffd0fbf529e227d977e0e3e5e75b4d93028abfe5ea69ee744acf42ca3d`;
Login5 container `e67beb5df399ac4ff3d2d3e406cf2fce79838f95e8f6fc8349f346e001648366`,
dossier `ev_f32638e39ba59f515d1e3492f9fb51c8037f4f3c969f721b0424c253855c53be`.
REA labels input files generically as shipped-artifact; these two containers
are explicitly analyst-derived and are corroborated by the original-DLL byte
Evidence above. Pseudocode is decompiler output, not original source.

Additional XPUI REA parent
`ev_83e2dab6818f6f161a5da66e62bd151f02b4744e3aa9b1b269ce1a7790b4fe65`
examined snapshot and mini-player snapshot: two parses, no failures, 269 files
unselected; semantic graph truncated at 3000 nodes. Its unresolved injected
modules remain unknown. No XPUI issuance JSON or guessed query hash is used.

The implemented wire contract is POST ClientTokenRequest to
`https://clienttoken.spotify.com/v1/clienttoken`, with Content-Type and Accept
application/x-protobuf and the native cache policy. POST LoginRequest to
`https://login5.spotify.com/v3/login` uses the same media/cache headers and a
scoped client-token, as the pinned Login5 reference specifies. The extra Accept
header is an explicit reference-based target choice. Neither request includes
an OAuth token, password, client secret, cookie or bearer Authorization header.
ClientData uses client version, client ID, device ID and truthful Windows data.
LoginRequest uses ClientInfo plus StoredCredential (tag 100), and preserves
login_context/challenge_solutions on follow-up. Request/response field numbers
come from the pinned definitions; native clienttoken descriptors independently
corroborate their core subset. Unimplemented derived-value fields and Login5 v4
are excluded rather than guessed. Clienttoken response handles grant/challenge;
Login5 v3 response handles ok/error/challenge. Native lite type names corroborate
the latter family; its complete wire schema is attributed to the reference.

## Implemented ownership and limits

One network owner serializes manager calls and the existing asynchronous HTTP
client. Fixed separate clienttoken and Login5 bearer slots retain client ID,
monotonic expiry, and refresh deadlines. Tokens are memory-only and never enter
logging or the public cache. OAuth refresh/access tokens and reusable AP
credentials retain their separate ownership from issue #10.

The secret arena is capped at 32 KiB, including the manager's bounded protobuf
scratch, HTTP upload/response buffers, hashcash context and token slots. The x64
manager occupies 26,928 arena bytes; the validator's temporary secret string
scratch adds 4,096 bytes (31,024 total), checked at compile time. The existing
credential owner retains its borrowed record; the shared HTTP arena accounts
for its own copied headers. Exported authorization headers are caller-owned and
must be cleared after the final completion. This module creates no heap
allocations, threads or handles. Oversized
responses, excessive domains/challenges, and unknown challenge kinds fail
explicitly. Unsupported code/JS/client-secret challenges never execute target
code. Limits are 1,023 clienttoken characters, 1,016 bearer characters, eight
domain grants, 4 KiB request / 6 KiB response, 1 KiB opaque Login5 context and
128 decoded prefix bytes. Hashcash supports one hashcash challenge per response,
two follow-up rounds, difficulty <=20, 64 attempts per poll, 65,536 default
iterations (configurable up to 262,144) and 2 seconds per challenge (up to 5).
Each acquisition flow has a 30 second total deadline. Hashcash follows the pinned
algorithm: SHA1(context)'s last eight bytes seed a big-endian uint64; suffix is
`(seed + counter modulo 2^64) || counter` (two big-endian uint64s). SHA1 of
prefix plus suffix must have the requested number of trailing zero bits.
Clienttoken prefixes and answers are hex; Login5 preserves binary prefix,
context and suffix and reports measured solve duration. These are explicit
bounded support choices, not claims about every official-client challenge.
Retain challenge state/context exactly through follow-up requests; wipe scratch
after use. Cancellation/logout retain WinHTTP-borrowed storage until completion
retirement and wipe it before arena reuse.

Repeated owner-thread requests join one in-flight refresh. A failed refresh is a
stable explicit outcome until an owner-authorized new attempt. Destination
admission requires an exact trusted service origin plus the clienttoken's domain
grant. CDN requests receive neither token. A request carries an authentication
generation and one-retry budget: a late 401 cannot discard newer tokens, a second
401 fails, and 403 never initiates refresh. A backward clock change fails closed;
UTC is not an input to expiry calculations.

Expiry uses the monotonic clock only. Clienttoken respects server refresh-after;
Login5 refreshes 30 seconds before expiry when its lifetime exceeds 30 seconds.
An expired token is never authorized. A 401 invalidates only the matching bearer
generation; if its clienttoken generation changed, it cannot discard the current
pair. An explicit retry retains the original request's single retry budget.
Failures latch and wipe both slots; `token_retry_refresh` admits a new explicit
attempt after outstanding HTTP retirement, so repeated consumers cannot cause a
retry storm. No automatic POST replay or redirect is enabled.

The account owner must stop AP use before logout, call `token_logout` to clear
the borrowed in-memory credential record, and delete persisted credentials with
`credential_store_logout`. Continue shutdown until it returns OK before reusing
the arena or stopping HTTP. Normal shutdown retains the account record for a
secure restart. Config origin/fixture strings and the credential record are
stable borrows through shutdown. Service admission also requires the shared HTTP
client's exact-origin sensitive-header allowlist; production needs the two auth
origins plus the selected service origins. CDN downloads use requests without
these authorization headers. Owner-thread callers queue concurrent needs to the
same manager; it is intentionally not a cross-thread mutable object. Idle wait
is INFINITE with no timer or handle; active HTTP exposes its borrowed event and
deadline, and only bounded hashcash work requests an immediate next step.

Offline fixtures are explicitly marked as synthetic credential substitutions
constructed from verified schemas/request assembly, with no claim of captured
successful service traffic. Live private-service acceptance remains separately
unverified unless an explicit account-dependent test is performed.

`api_tokens` compares exact POST bodies and headers against independent Python
protobuf/hashlib vectors, checks grant/refresh and rejection paths, deduplicates
24 queued consumers behind a held response, and checks generation-safe retry
and explicit failure recovery. Real asynchronous loopback callbacks exercise
cancellation and buffer retirement. Arena/handle metrics must balance after
every fixture; shutdown and logout assert secret zeroization. Unknown variants,
wrong usernames, unsupported JS/code, malformed messages, size/work/time caps,
clock rollback, domain spoofing and CDN admission are deterministic offline
checks. No optional live test is registered by this change.

## Verification recorded 2026-10-18

MSVC x64 C17 `/W4 /WX` builds passed in Debug with `ENABLE_ASAN=ON` and
optimized Release. The full Debug CTest suite passed 21/21, including
`api_tokens` and the three ASan arena tripwires; Release passed 18/18.
Tracked fixture allocations/handles balanced and no unexpected sanitizer
failure occurred. The token executable imports the ASan runtime and no C++
runtime. Pinned protobuf regeneration and the C-only build audit passed.
Live private-service acceptance was not run and remains unverified.
