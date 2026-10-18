# Modern Connect / Dealer (issue #20)

## Evidence and scope

This specification precedes the network implementation. Evidence is static;
account-free fixtures prove our implementation's behavior, not Spotify service
acceptance. The optional live check is a separate two-device acceptance step.

The selected installed Spotify 1.3.4.258 `Spotify.dll` has SHA-256
`ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde`.
Its original bytes contain `connect-state/v1/devices/` at `0x181b29518`,
`Spotify-Connection-Id` at `0x181b3eb38`, `X-Spotify-Connection-Id` at
`0x181c31bd0`, and `hm://connect-state/v1/player/command` at `0x181b2d040`.
The first header is received over Dealer; the X-prefixed header decorates HTTP
requests. They must not be confused. The cluster descriptor is at `0x181c2839d`.
Bounded REA records and exact original-byte mappings are retained in
`connect-evidence.json`. Inspection containers have synthesized PE headers and
unchanged selected original pages; missing code/data remain unknown. The full
native analysis was abandoned because of its cost; these are byte observations,
not native control-flow or execution evidence.

REA analyzed the current extracted XPUI's `187.js` and
`xpui-debug-window-contents.js`: parent
`ev_a6cce39f4dad87801f21052561fd70c0e64019f7859e42c3186e9d71ba2417d5`.
Both parsed without errors. The semantic graph hit its 5,000-node ceiling;
269 other files were outside analysis scope. These desktop consumers delegate
Connect to injected/native services; a source-wide literal search did not find
the Dealer endpoint contract. XPUI alone does not establish the native transport.

The complete request/message formats and methods below are supported by the
pinned librespot source at `19959512c212cbc48a3ff64382fe204f52c659a7`:
`core/src/dealer/{manager.rs,mod.rs,protocol.rs,protocol/request.rs}`,
`core/src/spclient.rs` and `connect/src/{spirc.rs,state.rs}`. These are independent
protocol reference evidence, not current-client runtime observations. The pinned
nanopb schema closure includes `connect`, `player`, `transfer_state`, `playback`,
`queue`, `context_track`, and their imports. Existing schema provenance and hashes
remain in `proto/manifest.json`. Current service acceptance remains unobserved.

## Transport and registration

1. Discover `dealer` and `spclient` with the existing ApResolve parser. No guessed
   fallback hosts. Production Dealer accepts a Spotify domain and TLS port 443.
2. Obtain a fresh Login5 bearer with token-manager's exact service-origin and
   granted-domain checks. Connect to `wss://{dealer}/?access_token={percent-encoded
   bearer}` using WinHTTP's HTTPS upgrade. No client-token is placed in that URL.
   Never log the path, token, raw frames, or authorization headers.
3. An initial `type: message`, `uri: hm://pusher/v1/connections/...` carries
   `headers["Spotify-Connection-Id"]` and often empty `payloads`.
4. Encode `spotify.connectstate.PutStateRequest` and PUT
   `https://{spclient}/connect-state/v1/devices/{device_id}` with
   `X-Spotify-Connection-Id`, `Content-Type: application/x-protobuf`,
   `Authorization: Bearer ...`, and the existing client-token header. Method and
   protobuf encoding are from the pinned reference; native literal observations
   corroborate the path/header. No GraphQL operation/hash applies.
5. One bounded HTTP request slot holds immutable upload/response storage through
   callback retirement. Coalesce newer state, refresh bearer once on 401, reject
   403, and re-register after every new Dealer connection ID. Registration
   succeeds only after a 2xx response. No fabricated discovery success.

Advertise only track playback, volume, observability, command acknowledgement,
command requests, finite transfer, logout and gzip. Rename, episodes, rooms, DJ,
set-options and other unimplemented capability flags stay false. State includes
the device ID/name, player position/state/volume and last accepted sender/message
ID; unsupported shuffle/repeat/queue-edit restrictions are encoded explicitly.

## Dealer envelopes

Messages: `{"type":"message","uri":"...","headers":{},"payloads":["BASE64"]}`.
Zero or one payload is supported. These Connect notifications use base64 binary
protobuf. Multiple payloads or an unsupported representation fail explicitly.
`Transfer-Encoding: gzip` applies **inside** the JSON envelope, after base64
decoding, independently of WinHTTP HTTP decompression.

Requests: `{"type":"request","message_ident":"hm://connect-state/v1/player/command",
"key":"opaque","headers":{"Transfer-Encoding":"gzip"},
"payload":{"compressed":"BASE64"}}`. The decoded request is UTF-8 JSON:
`{"message_id":20,"sent_by_device_id":"controller","command":{"endpoint":"pause"}}`.
Reply with `{"type":"reply","key":"opaque","payload":{"success":true}}`.
Return false for unsupported, invalid or full commands. Never acknowledge before
player admission. A successful admission guarantees one command consumption,
not successful audio decoding or account authorization.

The fixed 32-entry dedup window keys `(sender,message_id)`, survives reconnect,
and replays the previous outcome. FULL is retryable without side effects.
Keys are escaped during reply serialization. Unknown routes never execute.

## Supported controls and notifications

`resume`, `pause`, `seek_to.value` (nonnegative milliseconds), `skip_next`, and
`skip_prev` map to PlayerCommand. Direct track `play` accepts a track URI and
`options.{initially_paused,seek_to,skip_to.track_uri}`. Catalog/context resolution,
queue edits, repeat/shuffle overrides, local tracks and episodes are outside this
finite subset and return unsupported. Do not accept context options silently.

`transfer.data` is base64 `spotify.player.proto.transfer.TransferState`:
Playback holds current track, pause state and position; Queue holds following
ContextTracks. When `is_playing_queue` is true, the first queue item is the
current track (pinned `connect/src/state/transfer.rs`), and is consumed once.
Finite inline context pages resume after the current UID/track. External page
continuations and shuffled contexts are explicitly unsupported. Accept at most
64 tracks including the current track. Decode and
validate the complete queue before admitting a single PLAYER_TRANSFER command.
The player copies the transfer synchronously into one fixed slot, reserves that
slot until consumption, then atomically replaces queue/position on its owner.
Unsupported playback speed/options fail without modifying playback.

ClusterUpdate sets the active device; becoming inactive pauses local playback.
Stale cluster timestamps are ignored. SetVolumeCommand uses 0..65535. LogoutCommand
stops playback, closes Dealer, cancels state uploads and invokes token logout;
the account owner must also stop AP and delete persisted credentials (the token
manager's documented credential-store boundary). UI reads immutable snapshots;
all protocol mutation belongs to the network owner. Player publishes an
immutable queue/position/duration copy and signals a dedicated manual-reset
state event only when data changes. The network consumer atomically takes that
copy and resets the event. `dealer_wait_handles` combines it with shared HTTP
and WebSocket events; idle waits do not poll the player. PutState includes the
remaining finite queue and the decoder's duration when known.

## Bounds and lifecycle

Exactly two 128 KiB protocol blocks are borrowed exclusively: reassembly/input
and scratch. Payloads reserve yyjson padding within that ceiling. Scratch is
reused for envelope DOM, gzip output and command DOM. The inflater is fixed core
metadata; no miniz heap, zlib DLL, per-message heap or module thread. JSON depth
and pool bounds reject pathological input. Base64 is strict/canonical. Gzip
checks optional headers/FHCRC, reserved bits, complete DEFLATE, CRC32 and ISIZE;
concatenated/trailing members, truncation and expansion beyond capacity fail.

WinHTTP WebSocket keepalive is explicitly 30 seconds (the supported minimum is
15 seconds); native ping/pong frames are not exposed as application data. This
is separate from the AP Shannon keepalive. A 15-second connect deadline,
90-second complete-message receive deadline, 10-second send deadline and
3-second close timeout bound failures. Quiet connections conservatively reconnect
after the receive deadline even if native ping/pong remains healthy. Reconnect
uses a 10-second delay and fresh authorization before opening. Token refresh
deadline also retires the old socket. No storage or handle is reused until
terminal WinHTTP HANDLE_CLOSING callbacks retire; overdue retirement retains
ownership and waits on its event rather than polling/freeing live buffers.

WinHTTP option semantics: [Microsoft option flags](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags).

## Verification

`tools/connect_fixtures.py` reproducibly generates sanitized JSON/protobuf/base64
and gzip using independent Python encoders. Tests replay transfer, cluster,
volume, logout, controls, duplicate commands, fragmentation, malformed/oversize
input, gzip failures and reconnect/deadline/close behavior without an account.
Deterministic fake-device tests stay in the default strict-warning ASan suite.
The live two-device acceptance is explicitly opt-in; a skipped/unrun check is
not evidence of current Spotify compatibility.

The native loopback WebSocket test exercises a real HTTP upgrade, fragmented
text, interleaved ping/pong, writes, supported keepalive options and cancellation
of an outstanding receive. The HTTP fixture exercises connection-ID changes
while a PUT is in flight, 401 refresh/retry, 403 rejection and request retirement.

To compile/run the explicitly opt-in account test, configure with
`-DENABLE_LIVE_CONNECT_TEST=ON` and run
`ctest --test-dir build -R "^protocol_connect_live$" --output-on-failure`.
Supply private `SPOTIC_HEADLESS_CREDENTIALS` / `SPOTIC_HEADLESS_CACHE` paths plus
`SPOTIC_MEDIA_COUNTRY` / `SPOTIC_MEDIA_CATALOGUE`. Within five minutes a second
signed-in device must select the test player, transfer a finite queue, resume,
pause, seek, skip next/previous and change volume. Passing requires all seven
accepted control classes, an advancing real WASAPI timeline and balanced
shutdown. Missing environment returns skip 77. This test has been compiled;
no live/account compatibility result is claimed by the deterministic suite.

Legacy SPIRC (`ref/cspot/cspot/src/SpircHandler.cpp:76`) subscribes to
`hm://remote/user/{username}/` through Mercury. That distinct protocol is not
implemented here and is not a substitute for Dealer/connect-state.
