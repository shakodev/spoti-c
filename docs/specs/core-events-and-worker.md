# Core events, deadlines, and blocking work

Issue #3 supplies bounded owner notifications and one blocking art/cache worker
per instance. The implementation is C17 with Win32 SRW locks/events and C atomics
for cooperative cancellation. No application loop, HTTP endpoint, audio device,
or unbounded thread pool is introduced.

## Storage and handles

The setup owner supplies **one shared core arena of at most 128 KiB** for event
queues, job slots, owner metadata, and future logs/telemetry. Both creation APIs
charge metadata and storage to this arena. They reject an oversized arena and
return explicit invalid/budget/OS outcomes. Failed setup rewinds arena usage;
lifetime high-water still includes the failed attempt. There is no malloc or
automatic growth. Multiple setup arenas must not be used to bypass this combined
budget. All arena allocation remains on the setup owner thread.

Each event queue owns one manual-reset event; a worker owns a thread plus work,
active-cancellation, and idle events. Handles, OS bookkeeping, and the worker's
reserved/committed thread stack are separate OS costs to measure, not claimed
to fit inside the arena budget. Destroy/join close their handles; arena storage
is reclaimed only after every object and callback using it is quiescent.

## MPSC delivery and wakeup proof

An `Event` is a fixed-size value with type, generation, id, and four 64-bit
payload words. Payloads can identify externally owned resources; copying an event
does not retain or release those resources. Consumers validate generations before
applying asynchronous results to reused application slots.

Any number of non-audio producers may call `event_post`; one owner drains.
Queue-full is `CORE_FULL`, with nothing accepted and no overwrite. The caller
retains the event/resources and chooses retry, cancellation, or another explicit
backpressure policy. Posting and draining never allocate. This SRW-protected
queue must not be called from a WASAPI callback; that lane uses a dedicated event
and the lock-free PCM ring.

The producer acquires the queue lock, signals the manual event, commits the
record, and releases the lock. A woken consumer cannot acquire the lock before
the record is committed. The consumer removes a bounded batch and resets the
event only when the queue is empty, under that same lock. A producer either
commits before the empty check, keeping the event signaled, or acquires the lock
after the reset and signals again. There is no unlocked check/reset gap.
Partial drains keep the signal set while records remain.

`event_wait_handle` is borrowed and compatible with
`MsgWaitForMultipleObjectsEx`. Users must not reset or close it themselves.
`event_queue_close` stops admission and signals even an empty queue. Accepted
records remain drainable; `CORE_CLOSED` from drain means closed and empty after
that call, and the returned count may include the final batch. Closure stays
signaled until destruction. Destroy requires closure, an empty queue, and all
users quiescent.

## Jobs and cancellation

The single worker has a fixed caller-selected pool of at most 128 jobs. Submission
copies a fixed job description and returns a unique nonzero 64-bit ticket. Full
admission returns `CORE_FULL`; stopping returns `CORE_CLOSED`; ticket exhaustion
returns `CORE_EXHAUSTED` rather than wrapping and aliasing an old cancellation.
Queued or running tickets can be cancelled. A completed/reused-slot ticket is
never treated as a new job. Once a completion outcome is committed, cancellation
returns false for that ticket.

A job carries its request/generation, context, run and completion callbacks,
priority, earliest start, and deadline. Both callbacks execute on the worker
outside its lock. Every accepted job invokes completion exactly once, including
queued cancellation and expiry that skip the run callback. Ready disk work has
priority over artwork; tickets preserve FIFO within a priority. Running work is
not preempted. Completion may post to another owner queue, but must handle that
queue's explicit full/closed outcome without silently losing owned resources.

Cancellation is cooperative. A callback polls `worker_cancelled`, or waits on
the borrowed cancellation handle with `worker_cancel_wait` as its deadline
timeout, checking the token after waking. The token/context remain live through
the callback only. Blocking I/O must support cancellation or bounded waits.
The worker cannot forcibly cancel an arbitrary synchronous API. The idle handle
means no pending/running callbacks; it is not a barrier against new submissions.

Shutdown order is: stop admission/cancel, stop and join external submitters, join
the worker, finish draining completions, destroy destination queues, then dispose
or reset arena storage. `worker_stop` is synchronized with submission. `worker_join`
must run on the owner, never inside a callback. Timeout leaves all resources live
for acknowledgement and a later join; there is no forced thread termination.
Successful join closes all four worker handles.

## Monotonic scheduling and validation

Production milliseconds come from `GetTickCount64`, independent of wall-clock
adjustments. An injected reader must be monotonic, thread-safe, cheap, and
nonblocking. Deadline addition and exponential backoff saturate without integer
overflow. `CORE_CLOCK_NEVER` represents infinity; finite Win32 waits clamp below
`INFINITE`. A worker with no ready job sleeps until a submission/cancellation or
the nearest earliest-start/deadline time. An empty worker uses an infinite wait,
with no timer or polling loop. Injected-clock tests explicitly wake after an
advance; production deadline expiry uses the wait timeout itself.

`core_event_queue` races four producers against a capacity-one queue for 80,000
accepted records, forcing an empty/reset transition for every drain and verifying
per-producer order, generation, payload, and exactly-once delivery. A second race
closes admission while all producers are active and reconciles accepted counts.
Another four submitters race worker shutdown and reconcile every accepted job
against completion counts. Tests also cover full/partial/wrapped queues,
MsgWait compatibility, idle wait counts, active/queued cancellation, stale tickets,
join timeout/retry, ordered teardown, shared-budget exhaustion, fake expiry and
backoff, ready-job priority/FIFO, and a real delayed-job timeout without a wake.
Debug CRT hooks assert zero heap calls in watched queue/control/callback paths;
process handle counts balance across the entire suite. ASan checks buffer and
lifetime safety; lock reasoning and ordering stress provide separate concurrency
evidence, not an exhaustive proof of every OS scheduling interleaving.

References consulted without copying/modifying upstream code: librespot
`core/src/session.rs:683` expresses keepalive deadlines as sleeping state-machine
timers; miniaudio around line 24968 blocks on a WASAPI event; the pinned roadmap
requires one disk/art lane, bounded MPSC completions, and ordered shutdown.
Existing transport/playback and XPUI evidence remains unchanged.
