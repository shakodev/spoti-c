# Access point session (issue #9)

## Evidence gate and scope

This specification precedes implementation. REA checked the official client's
resolver host hint and directly exercised the official resolver before enabling
the default endpoint. No authenticated Spotify request or account login is part
of this investigation. The deterministic test suite remains entirely offline;
its success is not a claim of live AP/login compatibility.

REA inspected `ref/xpui/index.html` as inert text, directory identity
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`,
parent Evidence `ev_b74213bf2d20fa8b6bda64e26b4082db75bde4f675d06e7377299ec1bddcce82`.
It contains a preconnect hint for apresolve.spotify.com, without the endpoint
path, method, headers or response shape. The other 270 inventoried source files
were outside this selected REA pass. Supplemental literal searches are source
inspection, not a runtime observation or additional REA semantic coverage.
The native Spotify.exe was also selected for a resolver cross-check;
its SHA-256 is `06db5b888d52f7a3a4182cba18dfc8f4659646d2ff0110bb08586781a539782d`.
That bootstrap's defined-string inventory returned no apresolve match (Evidence
`ev_f64f16b3ec235b84e1b93c4fe5e5156ac5a3fcbc618a56d418e2f2430cf9ac43`);
this is not proof of absence. Its PDB identity is bootstrap.exe.pdb. The installed
Spotify.dll is the next explicitly selected application component, SHA-256
`ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde`.
Native Ghidra startup exceeded both 900-second and task-local 1800-second
allowances without usable partial Evidence. Additional task-local profiles
disable expensive whole-program analyzers; no native request/header behavior is
claimed from these incomplete runs, and no user configuration or target changed.
Those owned analyses were terminated after the direct resolver observation;
their output is not used as positive semantic evidence.

## Verified resolver contract

The complete portable REA observation is [ap-resolver-evidence.json](ap-resolver-evidence.json),
Evidence `ev_2845fa6e92a497a19a24dbbc4021f8c9e12173f6a89dff33e13e266dea615162`.
REA launched its own headless Chromium 155.0.8059.84 with empty storage and
service workers blocked, navigated once, waited one second, captured network
headers, the response body and DOM, and terminated its owned process.
This is a controlled request to the official service, **not an execution trace
of Spotify's native HTTP client**. The official xpui host hint and pinned
librespot request construction select the destination; the live observation
verifies that the request below is accepted by the official resolver.

- Method: `GET`, HTTPS host `apresolve.spotify.com`, path `/`.
- Exact query: `type=accesspoint&type=dealer&type=spclient`.
- No query hash, Authorization, ClientToken, cookies or account identifiers.
  Captured browser headers are retained verbatim in the evidence; they include
  the browser's ordinary User-Agent, Accept and fetch/navigation headers. They
  are not represented as native Spotify headers or copied into WinHTTP. No
  application-specific request header was needed in this successful observation.
- HTTP 200, `Content-Type: application/json; charset=utf-8`; top-level object
  contains `accesspoint`, `dealer`, `spclient`, each an ordered array of
  `host:decimal-port` strings. The recorded response had 6, 4 and 4 entries,
  respectively, and ports 4070, 443 and 80. Hosts are returned by the service,
  never hardcoded. Unknown object fields may be ignored.
- Implementation: ordinary WinHTTP GET with no added headers or body, existing
  TLS/origin/redirect rules, 10-second timeout and at most one HTTP retry. The
  existing HTTP client supplies its own User-Agent. Bound decoded JSON to
  16 KiB and each nonempty endpoint array to eight entries; reject overflow or
  malformed DNS host/port rather than truncate.

The body is Playwright `Response.body()` output, SHA-256
`a15ba5d066eecf202fbd22390fec1dfa6fa58ff5827db993f7c0cc3f80e53a3a`, 445 bytes;
the DOM independently contains the same JSON. REA explicitly does not guarantee
wire-byte identity or that Response.body() was obtained from the original
transaction rather than a browser follow-up load. This one service observation
does not establish future response stability, all regional endpoints, native
Spotify request headers, or authenticated transport compatibility. No native
`dealer-g2` behavior is inferred from adjacent binary strings; the implemented
`dealer` contract is the directly observed and pinned-reference form.

Reference-only source evidence is pinned to librespot
`19959512c212cbc48a3ff64382fe204f52c659a7`, specifically
`core/src/apresolve.rs:93`, `connection/handshake.rs`, `connection/codec.rs`,
`packet.rs`, and keepalive handling in `session.rs:683,755,909`.
Cspot `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`,
`cspot/src/ShannonConnection.cpp`, illustrates framing but incorrectly continues
after MAC failure. This implementation must close instead. Neither upstream
source is copied or translated wholesale.

## Wire and verification

Hello is `00 04 || u32be(total_size) || ClientHello`; total includes six framing
bytes. Select PRODUCT_CLIENT, Win32 x64, Shannon, server_keys_known=1, a fresh
16-byte nonce and one padding byte 1e. Version is a caller-supplied build value.
The repeated enum lists use legal protobuf packed encoding. The independent
Python oracle explicitly constructs that representation; the historical capture's
unpacked enum encoding is also valid protobuf, but is not reserialized into the
new hello or substituted for its transcript.
The AP response is `u32be(total_size) || APResponseMessage`, total at least 4 and
at most 65536. Preserve both complete wire messages in the transcript, including
their original framing and unknown protobuf fields; never reserialize the reply.

Decode bounded nanopb structures, require the DH challenge, check the pinned
RSA-2048 PKCS#1 v1.5/SHA-1 signature over the exact received gs, then validate DH
and derive the minimal unsigned big-endian secret. Only after verification may
HMAC-SHA1 derive the client challenge and independent 32-byte TX/RX Shannon keys.
Send `u32be(total_size) || ClientResponsePlaintext` with the DH HMAC and empty
required PoW/crypto response submessages. Unsupported challenge/upgrade forms
fail explicitly; there is no unbounded PoW work.

Frames are `Shannon(command:u8 || length:u16be || body) || mac[4]`.
TX and RX have independent nonces starting at zero, serialized big-endian.
Decrypt the three-byte header incrementally, bound the length before accepting
the body, and quarantine plaintext until the complete four-byte MAC verifies.
No command, callback or login state transition is dispatched before verification.
Bad MAC destroys the cipher state, wipes quarantine and closes the connection.
Nonce exhaustion requires a fresh handshake before wrap. TX backpressure and RX
delivery backpressure are explicit; partial writes never re-encrypt a frame.

## Owner, memory and states

One network owner drives resolve/connect/hello/challenge/framed/authenticated/
backoff/closed through ap_start, ap_send and ap_on_event. Raw Winsock sockets are
nonblocking, and DNS must also be asynchronous. Event handles and the next
deadline integrate with the owner's wait; the UI and WASAPI callback do no work.
Generation-tagged events cannot revive a cancelled or replaced session.

All application storage comes from the existing shared 256 KiB protocol arena:
separate maximum-size TX/RX buffers, a bounded handshake transcript and bounded
endpoint records. Resolver JSON uses caller-owned padded input and reuses the RX
buffer as its fixed yyjson pool while no framed connection exists. No hidden
heap growth or whole-track storage is introduced. Caller-owned transport seams
support deterministic fake AP replay without weakening production RSA checks.
OS DNS/CNG/socket bookkeeping is separate; explicit handles and arenas are
accounted, and cancellation retains pending DNS/HTTP storage until retirement.
Secrets, credentials, signed URLs and packet bodies are never logged.

Native DNS uses GetAddrInfoExW with an overlapped event and a five-second query
timeout. Its hostname, service, hints, timeout, overlapped object and result
pointer all remain in the owning socket record. Cancellation stops admission
immediately and lets that bounded query complete naturally before freeing its
result or invoking final WSACleanup. An immediate GetAddrInfoExCancel/WSACleanup
sequence produced a DNSAPI worker use-after-free under ASan on the target
Windows build; no sanitizer suppression is used. The corrected retirement path
passed 20 consecutive ASan executions. See Microsoft's
[GetAddrInfoExW contract](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfoexw)
and [WSAEventSelect contract](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaeventselect).
Both TCP and DNS progress belong to the network owner; only zero-time readiness
checks occur inside module calls. Wait handles and deadlines drive future work.
Shutdown permanently closes start admission, including while retirement is
pending; cancellation alone permits a later fresh-generation start after all
old work retires.

Login 0xab is supplied by the authentication owner after the framed transport
is ready. APWelcome 0xac authenticates only after a valid bounded protobuf and
an outstanding login. AuthFailure 0xad is routed distinctly from a transport
failure; credential acquisition/storage remains a later module.

Keepalive follows the pinned librespot policy: initial Ping deadline 20 seconds,
Pong four zero bytes 60 seconds after a valid four-byte Ping, PongAck deadline
20 seconds after sending Pong, and subsequent Ping deadline 80 seconds after
the ACK. All deadlines use the injected monotonic clock. Connection, handshake,
authentication and frame assembly also have finite deadlines. Endpoint failover
and backoff have fixed caps, and cancellation closes admission immediately.
Backoff is 250 ms exponentially capped at 2 seconds, within at most eight
caller-selected attempts (three by default), cycling through the bounded AP
list in resolver order. Every attempt uses fresh CNG DH material and nonce.
Malformed challenges, signatures and MACs fail closed; only transport failures,
keepalive timeout, nonce exhaustion and explicit TryAnotherAP trigger failover.
Login is not replayed automatically. A pending Pong behind blocked output uses
the finite TX deadline; when output drains, an immediate owner wake queues Pong
and starts its ACK deadline only after the Pong is fully written.

## Verification

protocol_ap_handshake replays independently encoded hello/response/transcript
fixtures, verifies signature-before-derivation, malformed lengths, every-byte
fragmentation, timeout/cancellation, wrong states and endpoint failover against
deterministic fake APs. protocol_ap_packet checks independent Shannon vectors,
partial writes, quarantined RX, corrupted MAC, maximum payloads and nonce limits.
Virtual-time keepalive tests cover each deadline and bounded retry behavior.
Default tests require no account or physical device. All tests inherit strict
compiler warnings and Debug ASan, and assert explicit resource balance.
The exact maximum 64 KiB AP reply includes a large unknown field, with keys
checked against the independent Python oracle; this verifies preservation of
unknown original wire bytes. Native loopback tests exercise overlapped DNS,
partial TCP exchange, cancellation retirement and bad-signature closure.
Controlled HTTP fixtures exercise resolver JSON, owned endpoint copies,
resolution-to-connect transitions and cancellation through HTTP HANDLE_CLOSING.
The explicit resolver URL override still goes through HTTP's HTTPS/origin policy;
cleartext fixture traffic requires that client's separate loopback opt-in.

Verified on Windows x64 with MSVC `/W4 /WX`, Debug ASan enabled:
`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON`,
`cmake --build build`, and `ctest --test-dir build --output-on-failure`.
All 18 tests pass with zero compiler warnings or unexpected sanitizer findings.
The three existing arena sanitizer tests intentionally trigger and verify ASan
diagnostics; normal AP tests finish with balanced tracked handles/allocations.
