# Transport and playback evidence

Historical source-reference survey from 2026-10-17. This file preserves the
initial reference observations and proposed policies; it is not the implemented
transport specification. Later module contracts take precedence:
[protobuf.md](protobuf.md), [ap-session.md](ap-session.md),
[oauth-credentials.md](oauth-credentials.md), [auth-tokens.md](auth-tokens.md),
[mercury.md](mercury.md), [playback-resolution.md](playback-resolution.md),
[audio-range-cache.md](audio-range-cache.md), [decrypt-stream.md](decrypt-stream.md),
[vorbis-stream.md](vorbis-stream.md), [audio-sink.md](audio-sink.md),
[player.md](player.md) and [connect.md](connect.md). In particular, CDN audio now
uses [Winsock/Schannel](cdn-transport.md); authentication/internal APIs use
WinHTTP. Original unresolved auth/API gates are followed up in
[auth-tokens.md](auth-tokens.md) and [pathfinder.md](pathfinder.md). Do not use
this initial survey to override later verified headers, request shapes or bounds.

Inspected 2026-10-17. These are observations from pinned local source, not proof of current Spotify server acceptance. cspot revision: `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`; librespot: `19959512c212cbc48a3ff64382fe204f52c659a7`. Paths below are repository-relative. The complete transitive proto inventory is [proto-inventory.json](proto-inventory.json).

## Protobuf selection

Use the full definitions from `ref/librespot/protocol/proto/`, with target-owned nanopb `.options`; cspot demonstrates nanopb integration but reduces important fields.

| Purpose | Exact files and closure |
|---|---|
| Handshake/auth | `keyexchange.proto`, `authentication.proto` |
| Mercury/pubsub | `mercury.proto`, `pubsub.proto` |
| Track/Album/Artist | `metadata.proto` (messages at lines 68/33/11) |
| Legacy SPIRC | `spirc.proto`; cspot `SpircHandler.cpp:76` uses `hm://remote/user/{username}/` |
| Current connectstate | `connect.proto` imports `player.proto`, `devices.proto`, `media.proto`; package is `spotify.connectstate`, not a file named `connectstate.proto` |
| Transfer/queue | `transfer_state.proto`, `context_player_options.proto`, `playback.proto`, `context_track.proto`, `play_history.proto`, `session.proto`, `context.proto`, `context_page.proto`, `restrictions.proto`, `play_origin.proto`, `suppressions.proto`, `instrumentation_params.proto`, `queue.proto` |
| Storage | `storage-resolve.proto` |
| Extended metadata | `extended_metadata.proto`, `extension_kind.proto`, `entity_extension_data.proto`, `google/protobuf/any.proto` |
| Clienttoken | `spotify/clienttoken/v0/clienttoken_http.proto`, `connectivity.proto` |
| Login5 | `spotify/login5/v3/login5.proto`, `client_info.proto`, `user_info.proto`, `challenges/code.proto`, `challenges/hashcash.proto`, `credentials/credentials.proto`, `identifiers/identifiers.proto` (relative to the login5 directory), and `google/protobuf/duration.proto` |

All 38 files in the closure are present locally, including well-known types. Do not generate the entire protocol directory. Callback-decode recursive metadata and repeated lists into bounded records; avoid cspot's `FT_POINTER` options. Keep legacy SPIRC separate from the modern implementation; `ref/librespot/protocol/build.rs:19` no longer generates SPIRC.

## Connection wire sequence

1. `ref/librespot/core/src/apresolve.rs:93`: GET `https://apresolve.spotify.com/?type=accesspoint&type=dealer&type=spclient`; parse three bounded arrays. Resolve candidates and connect a raw TCP socket, even on port 443 (`connection/mod.rs:67`). Retry deadlines must not block the UI.
2. `diffie_hellman.rs:8`: generator 2, 96-byte/768-bit prime. Client nonce is 16 bytes. ClientHello selects Shannon and `server_keys_known=1`; wire is `00 04 || u32be(6 + protobuf_size) || protobuf` (`connection/handshake.rs:151`). Preserve complete wire bytes for the transcript.
3. AP response is `u32be(total_length) || APResponseMessage`; total length includes its four bytes. Reject less than 4 or greater than 64 KiB before accepting body data (`handshake.rs:235`). Append length and body to the transcript.
4. Verify the pinned RSA-2048 PKCS#1 v1.5/SHA-1 signature over `gs` before DH derivation (`handshake.rs:22,83`). Validate DH peer values. cspot's reduced schema omits `gs_signature`: do not copy that omission.
5. Let `T` be hello wire concatenated with response wire, and `S` the minimal unsigned big-endian DH shared secret. Compute `D = HMAC-SHA1(S,T||01) || ... || HMAC-SHA1(S,T||05)`. Client challenge is `HMAC-SHA1(D[0:20],T)`; TX key `D[20:52]`, RX key `D[52:84]` (`handshake.rs:271`). The server checks this client HMAC; local AP authenticity is established by RSA verification, not an invented incoming HMAC field.
6. Send `u32be(4 + protobuf_size) || ClientResponsePlaintext`, including DH challenge and empty required PoW/crypto response objects (`handshake.rs:212`).
7. Initialize independent TX/RX Shannon state and u32 nonces at zero. Each packet is encrypted `command:u8 || payload_length:u16be || payload`, followed by four MAC bytes (`connection/codec.rs:9`). Encode nonce in big endian, increment once per packet, retain state across fragmented reads, and verify MAC before dispatch. Reconnect before nonce exhaustion. cspot `ShannonConnection.cpp:83` merely logs bad MACs; the target must close the session.
8. Send `ClientResponseEncrypted` as Login `0xab`; accept APWelcome `0xac` or AuthFailure `0xad` (`connection/mod.rs:108`). Access-token auth type is 3; stored Spotify credentials type is 1. Cache the *returned* reusable type/blob and canonical username. `session.rs:218` reconnects with reusable credentials after OAuth-token login for legacy keymaster compatibility.
9. Dispatch Ping `04`, Pong `49`, PongAck `4a`, CountryCode `1b`, ProductInfo `50`; maintain bounded keepalive/reconnect deadlines. Librespot's initial/expected ping timeouts are 20/80 s, delayed pong is 60 s, ACK timeout 20 s (`session.rs:683,945`). Cspot replies immediately; choose and test one documented behavior. Network deadlines remain active while rendering sleeps.

## Credentials and HTTP bridge

OAuth PKCE is a separate acquisition flow from AP auth, Login5 bearer renewal, and clienttoken. `ref/librespot/oauth/src/lib.rs:60,313,432` contains endpoints, PKCE, and redirect handling. Retain and validate CSRF state even though this reference discards it. Use the system browser, a bounded loopback listener, matching registered redirect/client ID, and DPAPI-protected refresh/reusable credentials. Never persist access tokens or signed URLs in the public UI cache.

Librespot `core/src/spclient.rs:146` POSTs protobuf to `https://clienttoken.spotify.com/v1/clienttoken`, with `Accept: application/x-protobuf`; request data includes client ID/version/device and Windows platform fields. Responses grant a token or bounded challenge; use `refresh_after_seconds` and domain scope (`:349`). `core/src/login5.rs:61` POSTs protobuf to `https://login5.spotify.com/v3/login` with `Accept: application/x-protobuf` and `client-token`; StoredCredential carries canonical username and reusable data. Stored credential/client ID pairing matters (`:163`). Internal spclient calls add `Authorization: Bearer ...` and `client-token` (`spclient.rs:494`). These are source-reference facts, not verified XPUI JSON/header contracts. Resolve the official-client gap recorded in [xpui-static.md](xpui-static.md) before implementation. Bound hashcash CPU/time and return explicit unsupported-challenge errors.

## Mercury and track resolution

Mercury uses commands `b2` GET/SEND, `b3` SUB, `b4` UNSUB, `b5` event. Frame body is `u16be(seq_length) || seq || flags:u8 || part_count:u16be || repeated(u16be(part_length)||part)`. Librespot uses an 8-byte request sequence. First part is `mercury.Header`. Flag 1 completes; flag 2 continues the last part in a later packet (`core/src/mercury/types.rs:76`, `mercury/mod.rs:175`). Subscription response may contain `pubsub.Subscription` URIs or be empty (`:121`). Bound all accumulation and request slots.

Modern metadata is POST `/extended-metadata/v0/extended-metadata` with `BatchedEntityRequest` (`core/src/spclient.rs:580`), not the cspot Mercury-only path. Match entity URI/kind/status, decode the `Any.value` into `metadata.Track`, and enforce country/product/availability restrictions and playable alternatives (`metadata/src/audio/item.rs:70`). Choose only supported OGG_VORBIS_96/160/320. Preserve original UI identity separately from the selected recording GID.

AudioKey is **not protobuf or Mercury**: command `0c`, payload `file_id[20] || track_gid[16] || seq:u32be || 00 00` (42 bytes). Response `0d` is sequence plus 16 key bytes; `0e` is error (`core/src/audio_key.rs:47,92,101`). Bound pending slots, timeout, cancel and late replies.

GET `/storage-resolve/files/audio/interactive/{file_id_hex}` on discovered spclient (`spclient.rs:773`); parse `StorageResolveResponse`, with `CDN=0`, `STORAGE=1`, `RESTRICTED=3`. Select unexpired `cdnurl` and re-resolve on expiry; do not hardcode CDN hosts. Cspot's `api.spotify.com/v1/storage-resolve/...?...product=9` is a historical alternative, not a current guarantee. CDN range requests are separate from bearer-authenticated internal requests and must not receive account credentials.

## Encrypted streaming and decoding

`core/src/spclient.rs:781` uses inclusive `Range: bytes=offset-(offset+length-1)`. `audio/src/fetch/mod.rs:103,434` uses 64 KiB blocks, 206/Content-Range verification and disk backing. Target design: eight 64 KiB RAM blocks, bounded encrypted disk cache, fixed request slots, and generation-tagged completions. Validate ranges, truncation, overflow, redirects and cache integrity; reject unsupported responses rather than caching an HTML/error body.

AES is `Ctr128BE<Aes128>` with IV `72e067fbddcbcf77ebe8bc643f630d93` (`audio/src/decrypt.rs:5,9`). For encrypted file offset `p`, counter is IV + floor(p/16); skip p mod 16 keystream bytes. The logical Vorbis offset zero maps to encrypted offset **167** after decryption (`playback/src/player.rs:51,1120`); never reset CTR at the Ogg origin. Four little-endian normalization floats are at decrypted offset 144 (`:350`); validate finite values before use.

`vendor/stb_vorbis.c:118,183,197` supports fixed decoder memory and push decoding; preserve unconsumed input and copy transient planar output before the next call. Push mode has no built-in sample seek (`:233`): implement bounded Ogg page/granule indexing, pre-roll, range resync and decode-discard. Vorbis packets are not inherently size-bounded (`:177`), so document clean rejection caps. Do not hold an entire track in RAM.

WASAPI callback consumes predecoded PCM and fills underruns with silence. Device/resampling work is outside the callback; `vendor/miniaudio.h:3639` describes native-rate/IAudioClient3 constraints, and `:24968` waits on audio events. Gapless uses current plus one prefetched decoder and a PCM frame boundary without stopping a compatible sink (`playback/src/player.rs:1603,1984,2111`). Seek/cancel requires an acknowledged epoch change before recycling live rings.

## Connect

Current `core/src/dealer/manager.rs:85` opens `wss://{dealer}/?access_token=...`; redact that query in logs. `connect/src/spirc.rs:183` handles connection IDs, clusters, volume, logout and commands. `spclient.rs:552` PUTs `PutStateRequest` to `/connect-state/v1/devices/{device_id}` with the connection-ID header. Decode transfer/queue protos, bound fragmented/base64/compressed payloads, acknowledge commands, deduplicate and reconnect. The legacy Mercury SPIRC transport remains a separately identified reference, not the modern primary path.

## Platform corroboration

CNG supports the DH size in principle; test the actual provider and custom group. [BCryptGenerateKeyPair](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgeneratekeypair). Raw shared secrets are little-endian, so reverse and normalize before transcript HMAC. [BCryptDeriveKey](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekey). Implement CTR using CNG AES-ECB counter encryption plus XOR without padding; CTR is not a documented CNG chaining mode. [CNG features](https://learn.microsoft.com/en-us/windows/win32/seccng/cng-features).
