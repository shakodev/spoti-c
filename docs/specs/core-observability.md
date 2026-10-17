# Structured logs, telemetry, and offline fixtures

Issue #4 establishes registered correctness tests and reusable fixtures for the
current core modules. It does not establish future network/audio/UI behavior or
claim that the process meets its eventual memory, CPU, or startup targets.

## Bounded logging and private values

`LogRecord` contains a monotonic timestamp, numeric subsystem/code, and four
typed numeric fields. The queue and metadata come from the same <=128 KiB core
arena used by queues and jobs. Capacity is fixed at setup and never grows.
Producers use a single SRW try-lock attempt; full or contended queues return
`LOG_DROPPED` and increment atomic drop counters. Producers never wait for the
queue lock, format strings, allocate, or invoke an output sink. They signal a
borrowed manual event so an owning wait loop can dispatch a flush job. Logging
does not spawn another thread or silently grow the worker job pool.

`log_flush` belongs on the non-audio worker. It consumes at most the smaller of
the requested limit and queue capacity, formats bounded lines, and invokes the
sink outside the queue lock. Producer posts and empty-event resets share the
same lock, preventing a lost notification. A failed sink consumes that record,
increments `log_write_failed`, and returns an explicit error; callers decide
how to report/recover. Closing keeps the event signaled and rejects new records.
Stop/join emitters, close, flush remaining records, and destroy before arena
reclamation. Sink and drain ownership remain single-consumer.

There is no arbitrary text, header, URL, format-string, or token-dump log API.
`log_private` accepts a classified private value but never reads, hashes, copies,
or retains it. It produces a zero-valued private field; an incorrect public or
unknown tag fails closed to `LOG_SECRET`. `log_emit` also zeroes values of private
fields, even if a caller constructs one without the helper. The worker prints
`[redacted]` for Authorization, client-token, OAuth codes, reusable credentials,
keys, signed URLs, Dealer URLs, and other private values. Public fields accept
numeric measurements/status/identifiers only; callers must not encode secrets
into a public number or subsystem/code.

Audio callbacks must not call logging, even its try-lock producer API. Their
observability path is only `metrics_count`: lock-free atomic increments and an
atomic overflow flag, with no lock, event, clock, callback, formatting, allocation,
or software retry loop.

## Accounting and audit boundaries

`metrics_attach_arena` binds before the first allocation and retains the existing
arena accounting hook contract. Aggregate current usage includes alignment
padding; `allocation_high_water` is the largest simultaneous observed arena
usage, retained after reset/rewind/disposal. Capacity, VM reservations, and VM
commits are separate fields. A prepared VM binding is passed directly into
reserve so its initial reserve/commit/release lifecycle is observed.

Explicit allocation and handle leases record ownership of resources outside
arenas. Leases are zero-initialized, never copied while live, acquired after a
successful OS allocation/open, and released only after a successful free/close.
Counts and high-water are checked for overflow. Invalid operations return false;
accounting corruption/overflow invalidates a clean shutdown claim. Atomic event
counters can wrap only with a sticky overflow flag. Live resource/arena updates
and snapshots use a short SRW lock; audio counter updates remain independent.
Counter fields in a snapshot are independently atomic rather than a transaction
across all active threads; final balance checks require quiescence.

Attached arenas automatically supply the metrics owner to event queues and
workers, so their actual event/thread lifetimes are tracked, including setup
failure cleanup. Worker notification/deadline wakes are counted. Render/underrun
counters are available for their future owners; no renderer or audio sink is
claimed to exist yet. Logging tracks its wake handle and writes/drops/failures.

`metrics_balanced` requires no live watched arenas, VM reservations/commits,
external allocation bytes/leases, handles, or accounting errors/overflow. It is
an ownership audit, not a process-wide heap scanner. OS/driver internals, thread
stacks, and any uninstrumented resource still require independent measurement.
Windows ASan is not treated as LeakSanitizer. Metrics control/binding storage
belongs to the caller and must outlive all observed objects and updating threads.

## Fixtures and registered tests

`FixtureClock` provides an injectable `CoreClock` with checked monotonic advances.
It does not sleep or implicitly notify a scheduler. Tests explicitly advance and
wake owners when needed. `FixtureTransport` is an injectable raw-byte exchange
function; tests can supply an entirely in-memory implementation.

The local HTTP fixture binds numeric IPv4 loopback on an ephemeral port, accepts
one connection/response per start, and copies one bounded response script. It
uses a 2 KiB request-header buffer, 2 KiB maximum scripted body, 4 KiB response
storage, nonblocking sockets, and stop-aware event waits. It supports held replies
for deterministic cancellation/shutdown tests. It has no external DNS/account,
TLS, or Spotify endpoint. It is not a production HTTP parser/client: raw response
bytes, truncation validation, and status interpretation belong to the consumer.
The test transport has a finite 3-second wait deadline and explicit cancel/full/
timeout/disconnect outcomes. Stop joins before reclaiming handles/storage;
client and server each balance their own Winsock reference.

The default suite now registers only tests that exist:

- `core_log`: a four-producer 100,000-record flood, an emit while a consumer lock
  is held, fixed arena usage, exact drop counts, worker-only formatting, all
  private field categories, sink failure, and shutdown/handle balance. CRT hooks
  assert zero heap calls on watched producer paths.
- `core_metrics`: arena reset/dispose peaks, VM reserve/commit/release accounting,
  external allocation/handle leases, a million concurrent underrun increments
  while the ownership lock is held, counter overflow, actual queue/worker handle
  integration, and final tracked/process handle balance.
- `fixture_http`: fake clock/transport injection, an actual loopback response,
  held-response release, request cancellation, server stop during a held request,
  output bounds, invalid script sizes, repeated idle server stops, and tracked
  resource balance after every scenario. Winsock is warmed before an additional
  process handle no-growth check because its provider can retire cached handles
  asynchronously. Exact owned-resource leases remain the decisive leak check.

All new targets inherit C17, strict warnings, and Debug ASan. Fixture networking
is local-only and stays enabled by default; no account, physical device, optional
GPU, or performance benchmark is registered by this issue. Existing smoke, arena,
SPSC, and event/worker tests remain enabled.

Reference inspected: `ref/librespot/core/src/token.rs` exposes sensitive access
token fields and a debug token dump. This implementation does not reproduce that
dump or its token acquisition endpoint. Existing pinned protocol/XPUI evidence
and upstream source/licenses remain unchanged.
