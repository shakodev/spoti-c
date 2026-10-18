# Playback metadata and storage resolution

Issue #13 investigation, 2026-10-18. This records static request construction,
reference behavior, and explicit implementation policy. No live private-service
acceptance, successful account playback, or captured request is claimed.

## Evidence and request gate

The inspected installed Spotify 1.3.4.258 `Spotify.dll` is SHA-256
`ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde`.
Focused Microsoft DUMPBIN x64 disassembly identifies the metadata request builder
at `0x1805592f0..0x18055967c`, storage path builder at
`0x1806b9d3c..0x1806ba0f8`, and storage request builder at
`0x1806ba3f0..0x1806ba65b`. Original-byte hashes and decoded embedded descriptor
fields are in [playback-native-evidence.json](playback-native-evidence.json).
`tools/playback_evidence.py` regenerates that manifest only for the pinned DLL;
when supplied REA records it checks every returned byte and source identity
against the original before retaining their Evidence IDs.

Original-DLL REA byte Evidence is metadata builder
`ev_1aea3c05ff747da0eb278845739c4fbc4e764c3e483e15d4926245aa4d9e97a4`,
storage path builder
`ev_4ee12df50d8335dc2ef36d3778953edaeb478bbda52b0843687936645b1fdf84`,
and storage request builder
`ev_dd7fb93b1c0777b24c25a468913d6fffa10fa665d570c13e54e3f299dbdd35f6`.
The Ghidra 12.1.4 byte profile digest is
`bbf5b701809af02de0028254d3f1b9bef8cf061e1cc5a84069704d35eb0315df`.
Automatic code/reference analyzers were disabled, as in the authentication
investigation. This establishes original byte identity for the supplemental
bounded disassembly, not a complete decompiled call graph. Descriptor Evidence
and all other range IDs are retained in the portable manifest.

The metadata builder explicitly selects POST via the static method object at
`0x18265fa88`, supplies `extended-metadata/v0/extended-metadata` at
`0x181a222a0`, inserts Content-Type and Accept `application/protobuf`, and attaches
the protobuf body through `0x1815703f0`. The same function separately constructs
`extended-metadata/v3/extensions/batch-get`. Its presence does not establish
which path a particular runtime session chooses. Issue #13 targets v0, whose
native schema and pinned reference request are independently available.

The storage builder constructs
`storage-resolve/v2/files/audio/{mode}/{format}/{hex40}?product=0`, with an
optional `&partner=` suffix. Mode helper `0x1806ba714..0x1806ba742` maps default/0
to `interactive`, 1 to `offline`, and 2 to `interactive_prefetch`. It converts
exactly 20 file-ID bytes into 40 hex characters. The file object's leading format
integer immediately precedes those bytes and is formatted into the numeric
segment. The native file-picker transfer below establishes its meaning. The
request builder inserts `cache-control: no-cache`, attaches its stored body
(empty for this GET target), and does not replace the default GET method.
Request constructors `0x18157b61c..0x18157b985` and
`0x18157b988..0x18157bd66` initialize their request using the GET object at
`0x18265fa48`; metadata explicitly replaces it with POST. There is no explicit
storage Accept or Content-Type insertion in this bounded builder.

Shared Authorization bearer and client-token insertion is established separately
in [auth-tokens.md](auth-tokens.md). The pinned librespot request path also adds
both headers in `core/src/spclient.rs:503..510`. These credentials belong only
to the admitted service origin. CDN URLs receive neither header. A trusted,
resolved spclient origin is caller configuration, not an arbitrary response URL.

REA inspected `ref/xpui/pip-mini-player-snapshot.js`, parent Evidence
`ev_f24caa9801af4a30e805d5e52b2e620aa8ba63eeade7cc2a5a4d46f3196b5a82`,
directory digest
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`:
one selected file, 270 unselected files, partial semantic graph at 1,000 nodes.
A separate bounded literal search of extracted XPUI JavaScript found neither
`extended-metadata` nor `storage-resolve`. This does not establish absence of the
native endpoints or resolve externally injected modules. Neither endpoint uses
a guessed GraphQL hash or an invented XPUI JSON envelope.

## Native protobuf envelope

The original DLL embeds complete FileDescriptorProto data for
`extended_metadata.proto` (file offset 30008800, 1521 bytes),
`entity_extension_data.proto` (30011344, 747 bytes), `extension_kind.proto`
(30337152, 6853 bytes), `storage-resolve.proto` (27502688, 261 bytes),
`audio_format.proto` (27786528, 777 bytes), `metadata.proto` (30348528, 9268 bytes),
and `media_manifest.proto` (28669072, 1250 bytes). Independent Python protobuf decoding
corroborates the pinned schemas' field numbers and enum values.

| Message | Verified wire fields |
| --- | --- |
| BatchedEntityRequest | header 1; repeated entity_request 2 |
| BatchedEntityRequestHeader | country 1; catalogue 2; task_id 3 |
| EntityRequest | entity_uri 1; repeated query 2 |
| ExtensionQuery | extension_kind 1; etag 2 |
| BatchedExtensionResponse | header 1; repeated extended_metadata 2 |
| EntityExtensionDataArray | header 1; extension_kind 2; repeated extension_data 3 |
| EntityExtensionDataArrayHeader | provider_error_status 1; cache TTL 2; offline TTL 3; extension_type 4 |
| EntityExtensionData | header 1; entity_uri 2; Any extension_data 3 |
| EntityExtensionDataHeader | status_code 1; etag 2; locale 3; cache TTL 4; offline TTL 5 |
| StorageResolveResponse | result 1; repeated cdnurl 2; fileid 4 |

TRACK_V4 is 10 and EPISODE_V4 is 12. The storage result enum is CDN=0,
STORAGE=1, RESTRICTED=3. Unknown enum numbers remain unknown; they are never
silently treated as CDN. Proto3's absent result is the defined CDN default,
while an empty URL list remains unresolved.

The legacy `spotify.metadata.AudioFile.Format` values are OGG_VORBIS_96=0,
OGG_VORBIS_160=1, OGG_VORBIS_320=2. Native helper
`0x18155cbec..0x18155cc23` reads the selected AudioFile protobuf's format at
object offset 0x20, or the corresponding media-manifest format at offset 0x18.
File-picker transfer `0x18155cc24..0x18155cdab` preserves that integer in r15d,
loads edx with it at `0x18155cd7b`, passes the raw 20-byte file ID through r8, and
calls `0x18155f6d8`. That constructor writes edx as the leading four bytes of a
24-byte file object, copies the 20 ID bytes immediately after it, and stores the
object at its output variant offset 8. The storage chain
`0x1806a6c14 -> 0x1806a52f0 -> 0x1806b9b30` takes that variant offset 8 and copies
the 24-byte object unchanged into the request. The constructor marks this branch
with variant tag 0 at output offset 0x88; storage admission tests that same tag
as `[variant + 8 + 0x80] == 0` before taking the file object. The path builder
formats its leading integer directly. Consequently this target uses format 0/1/2 in v2 for
Vorbis 96/160/320, respectively.

The independent media-manifest path `0x18069e33b..0x18069e3cc` hex-decodes exactly
20 ID bytes, reads its protobuf download-format field at offset 0x20, and calls
`0x18155cab4`; that constructor preserves the format at output offset 0x18, the
same offset consumed by the getter above. These bounded static transfers
establish the integer's source, rather than inferring it from adjacent storage.
The inspected `audio_format.proto` is actually in package
`spotify.stream_reporting_esperanto.proto`; its FORMAT_UNKNOWN=0 and Vorbis
1/2/3 values belong to reporting. That separate enum is not used for this route.

The response contains `google.protobuf.Any`; descriptors establish its typed
envelope, not a runtime-observed type_url string. Requiring the standard
`type.googleapis.com/spotify.metadata.Track` or
`type.googleapis.com/spotify.metadata.Episode` URL for the requested kind is the
implementation's explicit admission policy. Likewise provider status 0 and
entity status 200 are strict successful-response policy, rather than values
proven by a captured successful service response. The implementation must match
requested URI and kind, check both status layers, and validate Any before
decoding the payload. It must not select the last unrelated array element.

## Modern and legacy references

Pinned librespot revision `19959512c212cbc48a3ff64382fe204f52c659a7`:
`core/src/spclient.rs:580` POSTs the same v0 envelope using
`application/x-protobuf`; its modern metadata getter selects TRACK_V4 or
EPISODE_V4 and unwraps Any.value. Issue #13 uses the observed native
`application/protobuf` headers and adds strict response association/status/type
validation. Country/catalogue request context is represented by native schema
fields. Restrictions still have to be evaluated against the actual user.

That revision's `get_audio_storage` at line 773 uses the older
`/storage-resolve/files/audio/interactive/{hex40}` route without v2 or the
numeric-format segment. Its `core/src/cdn_url.rs` decodes the same storage
protobuf. Current native v2 construction is documented above separately; no
claim is made that an untested old route still accepts requests.

Pinned cspot revision `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`,
`cspot/src/TrackQueue.cpp:271`, uses
`https://api.spotify.com/v1/storage-resolve/files/audio/interactive/{hex40}?alt=json&product=9`,
with bearer Authorization, then takes `cdnurl[0]` from JSON. Its metadata uses
Mercury `hm://metadata/3/{track|episode}/{gidhex}`. Those are legacy reference
choices, not this module's native v0/v2 protobuf contract. JSON `alt=json`, the
fixed product=9, and the old Mercury metadata path are not mixed into the native
request target.

## Selection and URL lifetime policy

`metadata/src/audio/item.rs:70` corroborates positive duration checking,
restriction/availability checks, and track alternatives. A MediaSource keeps
the original requested URI while identifying the selected alternative's GID,
duration, format, file ID, key handle and URLs. The AP audio key request must use
that selected GID and selected file ID together. Only the three full Vorbis
formats are supported; previews, MP3/AAC/FLAC, external episode URLs and unknown
formats are not fallback audio sources.

Pinned librespot recognizes signed expiry forms `verify=<seconds>-...`,
`__token__=exp=<seconds>~...`, `Expires=<seconds>~...`, and a bare query beginning
`<seconds>_...`. It subtracts a five minute safety margin, but allows unknown
expiry to remain unlimited. Issue #13 explicitly requires a finite lifetime:
bound any unrecognized lifetime locally and refresh through storage resolve
before reuse after its deadline. CDN URL query credentials are private data;
fixtures use synthetic signatures and reserved `.example` hosts, with no copied live
account material. CDN, STORAGE, RESTRICTED, unavailable/expired URL lists and
unknown results are distinct outcomes. There is no indefinite signed-URL cache.

Live account-dependent acceptance, runtime v0/v3 selection, all optional native
headers, and unsupported codecs remain unverified. Offline tests establish this
module's deterministic behavior against independently constructed protobuf
fixtures; they do not turn synthetic responses into captured Spotify traffic.

## Bounded module and owner integration

`spotic_media` owns four source slots and one serialized 64 KiB HTTP response
buffer. Request bodies use 512 bytes. Text IDs are exactly 22 base62 digits
(128-bit overflow rejected), 16-byte GIDs or 20-byte file IDs; hex conversion is
length checked and transactional. A source preserves its 36-byte original URI,
selected recording/file, Vorbis format, positive duration, a 16-byte generation
key handle and up to four 2047-byte URLs. The entire resolver fits the 256 KiB
metadata budget; it does not create threads or OS handles or grow storage.

Metadata selects the original recording first, then playable alternatives. ID-only
alternatives receive their own URI-associated metadata requests; a visited set
bounds the complete resolution to 16 IDs/four levels. Each response additionally
bounds eight alternatives per node, 16 total nodes/four levels, 64 KiB wire bytes
and schema-aware field counts. Requested-root explicit filtering is terminal;
country/product restrictions and unavailable recordings may relink. Unknown
restriction types and sale-period policy fail closed. ISO country lists have
a dedicated static 512-byte cap, sufficient for complete two-letter market lists,
rather than the general 256-byte text limit. Any values are decoded with bounded
nanopb wire primitives so the generic 257-byte Any struct does not truncate Track.

The independent `spotic_audio_key` module admits eight requests. Command `0c`
is exactly `file_id[20] || selected_gid[16] || sequence:u32be || 00 00`.
Commands `0d` and `0e` require exactly 20 and six body bytes, respectively.
The effective deadline is the earlier of the caller deadline and the configured
timeout (default 1500 ms). IDs burn on rejected sends, never wrap, and never replay.
`ap_dispatch_audio_key` requires an authenticated AP session; ingress peeks actual
MAC-verified AP storage before callbacks. Incomplete/corrupt packets never expose
key bytes. The key callback immediately copies its borrowed key into the source.

Create both modules on the network owner. Connect AudioKey after APWelcome,
dispatch `0d`/`0e` from the AP packet callback, and disconnect keys when AP closes.
Configure the exact discovered spclient HTTPS origin in both HTTP and token
allowlists. `media_source_resolve` returns an admitted ID; `media_source_poll`
advances HTTP/token/key work without blocking. Wait on AP and HTTP handles using
their nearest deadlines plus `media_source_next_wait_ms`, which also includes
HTTP, token and AudioKey deadlines. Backpressure waits for owner readiness,
rather than periodically polling. Each source has its own one-retry token
authorization state. HTTP redirects are disabled. The explicit loopback fixture
origin permits synthetic unauthenticated tests only; production requires tokens.
The module does not fetch CDN audio or forward service credentials to CDN URLs.

Signed expiry forms are parsed after splitting raw query components; encoded
ampersands cannot introduce new parameters. Multiple recognized deadlines use
their minimum. URLs refresh five seconds before their signed deadline, with a
60-second maximum local lease also applied to unknown expiry formats. Deadlines
are converted once from UTC to the monotonic owner clock. Expired URLs are never
returned. Refresh sends only storage resolution and keeps existing valid fallback
URLs and key leases usable. CDN/STORAGE/RESTRICTED/unknown/expired outcomes stay
explicit. No successful resolution or URL is cached indefinitely.

`media_key_acquire` returns a unique bounded ticket and a stable key pointer.
Workers acknowledge quiescence through `media_key_release` on the network owner.
Retirement cancels new work and pending HTTP/AudioKey requests, closes admission
to new leases, and preserves existing key bytes until every exact ticket returns.
At most 16 leases exist. Duplicate/copied stale tickets cannot decrement a new
lease. Source slots wait for HTTP callback retirement and zero remaining leases
before reuse, then zeroize the key. Shutdown returns PENDING until this completes;
only then can the caller dispose its arenas. Mutating callback reentry is rejected.

## Verification

Default ASan tests are `protocol_media_source` and `protocol_audio_key`, including
literal request fixtures, metadata association/policy/relinking, malformed lengths,
MAC tampering, all capacity limits, late replies, deadlines/cancel/reconnect,
four signed expiry forms, conflicting deadlines, URL refresh, lease retention and
zeroization, and balanced arena/WinHTTP/socket handle accounting. Module arena
use never increases after creation. AudioKey also checks zero Debug CRT heap
allocations. WinHTTP's internal OS allocations are outside that module ledger.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The real-account `protocol_media_live` target is absent unless explicitly enabled
with `-DENABLE_LIVE_MEDIA_TEST=ON`. It compiles under the same strict warnings and
ASan settings. Set `SPOTIC_OAUTH_CLIENT_ID`, `SPOTIC_OAUTH_REDIRECT_URI`,
`SPOTIC_OAUTH_SCOPES`, `SPOTIC_MEDIA_TRACK`, `SPOTIC_MEDIA_COUNTRY` and
`SPOTIC_MEDIA_CATALOGUE`, then select that CTest target. It performs browser OAuth,
AP login, discovered-origin token authorization, metadata/AudioKey/storage
resolution and lease retirement without logging keys or signed URLs. Missing
configuration returns skip code 77. Building this probe is not live acceptance;
no account-dependent run was performed for this change.
