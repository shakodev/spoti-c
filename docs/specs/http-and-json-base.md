# Bounded HTTP transport and protocol JSON

Issue #8 implements a generic transport, with no Spotify endpoint, token exchange,
PathFinder hash or application JSON contract. Endpoint implementation still needs
the REA-backed specifications required by AGENTS.md.

## Evidence and platform contract

Consulted pinned librespot revision `19959512c212cbc48a3ff64382fe204f52c659a7`:
`ref/librespot/core/src/http_client.rs` (shared client, status classification and
Retry-After), `core/src/spclient.rs:438` (bounded request strategies and separate
Authorization/client-token headers), and `audio/src/fetch/mod.rs:434` (stream
headers and 64 KiB blocks). The upstream HTTP client's unbounded 429 retry loop
is deliberately bounded here. No Rust/C++ source is incorporated.

Microsoft's [WinHttpCloseHandle documentation](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpclosehandle)
requires context/storage retention through HANDLE_CLOSING, even after close
returns. [Concurrency in WinHTTP](https://learn.microsoft.com/en-us/windows/win32/winhttp/concurrency-in-winhttp)
permits cancellation of an outstanding operation by closing its handle.
[Option flags](https://learn.microsoft.com/en-us/windows/win32/winhttp/option-flags)
document header limits, gzip/deflate decoding and disabling automatic redirects,
authentication and cookies. OS certificate/hostname validation is retained; no
certificate errors are ignored. The session uses direct connections, with no
automatic proxy discovery or implicit account credentials.

Native fixture observation on Windows: automatic decompression removes
Content-Encoding and Content-Length from the visible headers. Consequently a
header-only expansion check is insufficient. Before accepting each nonempty
read, the owner queries [WINHTTP_REQUEST_STATS](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ns-winhttp-winhttp_request_stats)
and compares decoded bytes to
[WinHttpResponseBodyCompressedSize](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/ne-winhttp-winhttp_request_stat_entry).
The decoded body cap applies independently, including to unknown-length bodies.
Unavailable accounting on a nonempty read fails explicitly with HTTP_ENCODING;
this establishes Windows 10 1903 or later as this module's platform requirement.
Unknown remaining Content-Encoding values are rejected. Encodings left visible
by the OS must have a nonzero bounded Content-Length. Both fixed-length and
EOF-delimited native gzip decoding are tested, including a 44-byte fixture that
expands to 8192 bytes and exceeds the default ratio of 16.

## Ownership, admission and callback retirement

The single network owner creates one HttpClient in the shared HTTP arena, capped
at 384 KiB. Metadata, eight slots, eight host records, the MPSC queue and eight
8 KiB read staging buffers fit in that arena (`sizeof(HttpClient)` is statically
asserted). There is no module heap allocation or module worker thread. Caller
body/upload storage is explicit and separate; OS WinHTTP/TLS allocations and
stacks are OS costs, not claimed to fit this application ledger. Metrics track
every explicitly owned event, session, connection and request handle. Arena
metrics account for backing storage using the existing core hooks.

All public operations belong to one network owner. Callbacks copy only slot,
generation, attempt, status, byte count and error into a locked MPSC queue.
Callback data pointers are never retained. Reading fills a slot's fixed staging
buffer; its storage is retained until the final callback is retired. The lock
release after enqueue is the callback's last access to application storage.
The owner drains using that lock. No callback advances the state machine or
starts another WinHTTP operation.

At most one asynchronous operation is outstanding per slot. Only send completion,
headers, read completion, request error and handle closing are subscribed; handle
creation is ignored. Before a new operation is issued its previous descriptor
has been removed. Cancellation can add error and closing notifications. Thus
at most three descriptors per slot (24 total) can accumulate; the queue holds
32, with an explicit contract-violation error instead of silent overflow.

`http_cancel` closes admission to further work on the individual request handle;
it does not release the slot, host lease, upload buffer or body buffer.
HANDLE_CLOSING releases the handle metric and host lease, then publishes the
terminal result. Only terminal consume frees admission. Generation and attempt
checks reject stale descriptors. Generation counters never wrap; exhausted slots
cannot be reused. Shutdown closes admission, cancels requests, drains final
callbacks and discards results before closing host/session/event resources.
Shutdown timeout retains the entire client and borrowed storage for a retry.

## Bounded progress, retries and redirects

Buffered requests default to a decoded limit of 1 MiB, further constrained by the
supplied buffer capacity. Streaming requests use the supplied 64 KiB block and
the same default total limit unless the caller explicitly chooses a larger one.
A full block pauses further reads until consume. Returned strings/body bytes
are never NUL-terminated implicitly. A terminal failure can report a partial
body; callers must check the result before using it. HEAD does not read a body.
Completions also borrow bounded raw response headers from the slot, allowing
future callers to validate Content-Range and other endpoint-specific fields.
Copy fields before the slot can be reused by another submit, or before shutdown.

The event is manual-reset; synchronized enqueue/drain/reset avoids lost wakes.
An empty client has an infinite next wait and an unsignaled event. `http_wait`
blocks until a callback, the nearest total deadline/backoff or the caller's
timeout. Alternatively the owner integrates the borrowed event and computed
timeout with MsgWaitForMultipleObjectsEx. Pump handles one descriptor so callers
can bound work. Total deadlines include retries, redirects and block backpressure
and take precedence over queued success. Native resolve/connect/send/receive
timeouts are additionally 5 seconds each. Rendering needs no periodic poll.

401/403 are auth failures, 429 rate limit, 5xx server failures, and other failures
retain their numeric HTTP status. Only GET/HEAD retry 429/5xx, with at most three
retries. Backoff is 250/500/1000 ms, capped at 30 seconds; Retry-After seconds or
canonical IMF-fixdate can extend but not shorten it. Excessive delays clamp to
30 seconds and remain subject to the total deadline. No mutating upload is
automatically replayed and no stream is retried after delivery.

URLs are bounded, HTTP(S) only, with no userinfo, fragment, control characters or
backslashes. Production requires HTTPS; fixtures explicitly opt into literal
127.0.0.1 HTTP. Caller headers require an exact scheme/host/port allowlist and
bounded token names/values; framing/proxy/encoding control headers are forbidden.
Only GET/HEAD follow 301/302/303/307/308, capped at five redirects. Absolute and
root-relative destinations are supported; other relative forms fail explicitly.
HTTPS downgrade is rejected. Any origin change permanently clears every caller
header, including cookies, client-token and arbitrary private headers. Automatic
cookies/authentication/redirects are disabled. Unused host records may be evicted,
but active leases persist until request callbacks retire.

## JSON scratch and borrowed readers

The already pinned yyjson C source and license are integrated directly into the
C build. `json_base_parse` uses its fixed caller-owned pool allocator, with no
malloc fallback, and a caller-owned in-situ buffer padded by YYJSON_PADDING_SIZE.
The disjoint input capacity plus allocator capacity must fit the existing shared
256 KiB protocol scratch allowance. These allocations do not create a new budget
or allow bypassing other protocol users of that scratch. Input is at most 16 KiB
and the DOM at most 4096 values. Callers must close before reusing either buffer.

Strict JSON and UTF-8 parsing rejects malformed numbers, trailing content, invalid
UTF-8, isolated surrogates and nonstandard numeric literals. Field/type/string,
unsigned integer and boolean readers return distinct missing/null/type/range
results. Strings carry explicit lengths, may include NUL, and borrow the input.
An explicit copy operation produces caller-owned C strings and rejects embedded
NUL. No retained multi-page model is introduced.

## Deterministic verification

`api_http` uses a stop-aware, nonblocking loopback fixture with scripted responses
and gates. It covers fragmented/chunked/partial bodies, limits, compression,
backpressure, exact pool admission, cancellation and shutdown at send, headers,
read, blocked-stream and closing phases, stale generations/attempts, deadline
precedence, retry caps/backoff cancellation, cross-origin stripping and redirect
caps. Each scenario requires exact tracked handle/allocation balance and checks
for an idle unsignaled event. These are offline checks, not Spotify compatibility
or external TLS/account tests. `api_json_base` checks escaped/invalid UTF-8, null,
missing/types, numeric boundaries, malformed JSON, pool exhaustion, DOM/input
limits, padding and borrowed versus copied ownership. Both targets inherit the
strict warnings and Debug ASan options and run in default CTest.
