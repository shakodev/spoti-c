# Bounded Mercury transport

Issue [#12](https://github.com/shakodev/spoti-c/issues/12). Source evidence inspected
2026-10-18, against librespot `19959512c212cbc48a3ff64382fe204f52c659a7`
and cspot `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`.
This implements legacy AP Mercury, with no new HTTP/GraphQL endpoints. Source
fixtures establish framing and ownership behavior; they do not establish live
Spotify server compatibility.

## Evidence and wire contract

- `ref/librespot/core/src/mercury/types.rs:76`: requests have a two-byte big-endian
  sequence length, sequence, flag byte, two-byte part count, and length-prefixed
  parts. Request sequences are eight-byte big-endian integers. The first part is
  nanopb `mercury.Header`; GET/SEND use `b2`, SUB `b3`, UNSUB `b4`.
- `ref/librespot/core/src/mercury/mod.rs`, `dispatch`/`complete_request`: `b5` is
  an unsolicited event; flag `1` completes the response, `2` leaves the final part
  open and joins it with the next packet's first part, and `0` retains completed
  parts for a later packet. Parts may include empty payloads. An empty Header is
  rejected. A final zero-part packet can finish previously completed parts.
- The same file's `subscribe` handles a list of encoded `pubsub.Subscription`
  payloads, or the original requested URI when the response payload list is empty.
  Event URIs match subscription prefixes after component-wise form URL encoding,
  including UTF-8 bytes. Multiple aliases of one subscription notify it once.
- `ref/librespot/protocol/proto/mercury.proto` and `pubsub.proto`: Header status
  is **sint32**; subscription payload status is **int32**. Decode through the
  existing bounded nanopb closure. Like the pinned reference, `expiry` is decoded
  but does not establish a renewal policy. HTTP-like status >=400 completes
  requests with `AP_IO_ERROR` and retains status/payload in the callback response.
- `ref/cspot/cspot/src/MercurySession.cpp`, `handlePacket`/`decodeResponse`,
  corroborates command routing and sequence/part lengths. Its separate AudioKey
  handlers are not included in Mercury. Its unbounded containers are not ported.

## Ownership, bounds and results

One network owner creates the client in an owned arena. There are 16 request
slots, eight subscription records (each at most eight aliases), four unsolicited
event assemblies, and one shared 131,072-byte multipart pool. A response includes
at most 16 parts including Header, 32,768 accumulated bytes, and 64 packets.
Text is at most 256 UTF-8 bytes. A compile-time assertion bounds the entire client,
including its 65,535-byte send scratch, within the 256 KiB metadata budget.
These reservations are separate from the AP wire/transcript budget.

The pool compacts on append/release, so live bytes determine capacity regardless
of completion order. No packet operation calls heap allocation, reserves handles,
or allocates from the arena. Stats expose live counts, pool use and high-water.
Admission failures leave output IDs unchanged and never grow storage. Packet
limit/full/malformed failures retire the affected request and release its bytes;
other requests remain intact. Unknown request sequences and unmatched/replayed
events return `AP_STALE`; unsupported commands return `AP_UNSUPPORTED`.

Request IDs are monotonically increasing wire sequences, burned even on transport
rejection, and never wrap. Responses match command and sequence. Subscription IDs
remain stable across replay; replay receives a fresh wire sequence. Events capture
the active subscription IDs on arrival so a partial event cannot notify a later
occupant of a reused subscription slot. A bounded cache of 16 retired event
sequences rejects repeated completions and late tails, including after timeout.
Mercury has no fragment index: fragments of one sequence assemble in authenticated
AP arrival order, while responses to different requests may finish in any order.
Shannon provides ordering and authentication; Mercury cannot independently
identify an application-level duplicate middle fragment with identical framing.

Callbacks run once for each terminal request outcome. They borrow all Header and
part spans only until return; consumers copy bounded records. Mutation/reentry
from callbacks or the send adapter returns `AP_BAD_STATE`, preserving span
stability during pool compaction. Completion callbacks carry `AP_TIMEOUT`,
`AP_CANCELLED`, or the explicit protocol/capacity error on failure. Cancel accepts
a pending request or stable subscription ID. Active subscription cancellation is
local; use UNSUB to send remote cancellation. Successful UNSUB admission stops
local events immediately, even if its later response fails or times out.

## AP owner integration and reconnect policy

The owner supplies a synchronous copy/enqueue adapter to `ap_send`. A return of
`AP_OK` accepts the entire frame; other returns accept nothing. The adapter must
not reenter Mercury. Queued application work runs after callbacks return.

1. After verified APWelcome, call `mercury_connect(client, ap_generation(session))`.
2. In the AP `on_packet` callback for `b2`..`b5`, call
   `ap_dispatch_mercury(session, client)`. It requires an authenticated AP session
   and passes its actual `ApPacket` to Mercury. `mercury_on_packet` calls
   `ap_packet_peek`, so incomplete or bad-MAC packets cannot deliver callbacks.
   Neither helper consumes AP's borrowed packet.
3. Include `mercury_next_wait_ms` in the owner's nearest-deadline wait. Run
   `mercury_tick` on due timers and transport readiness. Finite absolute deadlines
   use the injected monotonic clock; `CORE_CLOCK_NEVER` requests the bounded
   default timeout (10 seconds unless configured). Event deadlines start at the
   first packet and never slide on additional fragments.
4. After an AP operation closes or enters backoff, call `mercury_disconnect` once
   outside the AP callback. This retires requests and event assemblies immediately.
   Set `reconnect=true` to notify `AP_RECONNECT`; final shutdown notifies
   `AP_CLOSED`. Non-idempotent GET/SEND/UNSUB operations are never replayed.
5. With `resubscribe=true`, retain only subscription intent/callbacks. Connect/tick
   sends fresh SUBs with new deadlines; routing resumes after successful SUB
   acknowledgement. `AP_FULL` waits for transport readiness instead of returning
   a zero-time polling deadline. Other send errors are returned to the owner.
   With replay disabled, disconnect removes all subscriptions. Final disconnect
   releases every borrowed context before arena disposal.

## Deterministic verification

`protocol_mercury` uses literal GET, SEND, SUB, UNSUB, response and event fixtures;
split Header/body parts; out-of-order completions and duplicate sequences;
subscription payload aliases and empty fallback; stale subscription generations;
MAC tampering and fragmented authenticated ingress; malformed/truncated/zero and
overflowing lengths; request/subscription/event/part/fragment/response limits;
full shared pool and compaction; cancellation, deadlines, reconnect, and blocked
replay. The pool is filled to exactly 128 KiB and returns to zero on cleanup.
Arena accounting remains balanced, arena use never changes after creation, and
MSVC Debug CRT hooks assert zero heap allocations throughout packet/lifecycle work.
The existing nanopb build rejects dynamic protobuf allocation.

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build -R "^(protocol_mercury)$" --output-on-failure
ctest --test-dir build --output-on-failure
```
