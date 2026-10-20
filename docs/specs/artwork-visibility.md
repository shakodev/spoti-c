# Native artwork visibility and failure lifecycle (#42)

Written before implementation on 2026-10-20. This specifies the local C
pipeline, not a newly reverse-engineered Spotify endpoint or runtime behavior.
Centered cover cropping and the caller-selected corner radius are native
presentation contracts consistent with the #41 component foundation. Exact
Spotify runtime geometry remains subject to the reference verification plan.

## Existing source evidence

Inspection of `src/ui/artwork.c`, `src/ui/texture_cache.c`,
`src/ui/image_decode.c`, `src/core/worker.c`, and `tests/test_artwork.c` found:

- The artwork worker serializes fetch and decode into one fixed workspace and
  one upload buffer. UI preparation uploads at most one completion and starts
  at most one job. Rendering does no fetch, decode or resource creation.
- Navigation cancels obsolete generations, but `artwork_begin_visible` only
  unpins cache entries. Same-route offscreen requests continue downloading.
- Failed fetch/decode requests are forgotten after completion. Repeating
  requests on each visible preparation can therefore produce a retry loop.
- Cache handles validate slot, serial and backend texture validity. Releases
  are deferred to a preparation boundary. A serial survives cache clear, so a
  recycled slot does not make an old handle valid again.
- Decode retains the image aspect ratio. Cache entries omit its output width
  and height, and `artwork_draw` uses the full UV range: nonsquare images are
  stretched into square cards.
- Worker cancellation is cooperative. Completion commits an outcome before
  invoking the callback; cancellation may arrive after that commit. An unseen
  completion must therefore be rejected on the UI thread as well.

## Visible-set contract

At a safe boundary after the previous frame has been consumed, call
`artwork_begin_visible`, then request or look up every desired image, and then
call `artwork_prepare`. Lookup marks a pending identity even if no GPU texture
exists yet. The caller is responsible for deriving the desired set from the
viewport, including any deliberately chosen prefetch range.

Preparation finalizes this set. Unseen queued requests are removed without
fetching. An unseen deferred upload is discarded. An active unseen request is
cancelled cooperatively and its request URL, identity and buffers remain intact
until its callback retires. A late successful completion for an unseen image
cannot upload. Existing cached offscreen textures remain available to bounded
LRU eviction; no resource is destroyed during frame construction.

When a cancelled image becomes visible again before its callback retires, the
same request slot is retained. After completion it can be queued once more.
This prevents a placeholder from becoming permanently stuck after rapid
scrolling. Calls that do not start a visible pass retain their existing
request/prepare behavior for compatibility.

## Failure and explicit retry contract

Store terminal fetch/decode/upload failures in a fixed 64-entry negative cache,
keyed by the complete content/size/navigation-generation identity. Records
remain until an explicit retry or a newer navigation generation; neither time
nor drawing clears them. Repeated ordinary requests return the recorded result
and do not enqueue work.

Admission reserves enough record capacity for all pending/active requests, so
failure-table pressure returns `ART_FULL` before admitting a request that could
not be remembered. Negative records are never evicted to make room for another
automatic attempt. Transient queue/GPU capacity pressure remains pending, and
visibility cancellation is not a terminal content failure.

`artwork_retry` clears only the selected failure and performs no I/O. The next
ordinary request is the one new attempt. `artwork_error` reports a remembered
failure without marking visibility or scheduling work. Navigation clears old
failure records, while old-generation requests and handles remain rejected.

## Sources and completion notification

The requested decode edge remains at most 256. A source with known dimensions
can have an original edge up to the decoder's existing 512 limit: prefer the
smallest known source at least as large as the requested edge, otherwise the
largest smaller source. A source with both dimensions zero is an unknown-size
fallback, using the first such source only when no admissible known source
exists. A single zero dimension or an original edge above 512 is not
admissible. No dimensions or URLs are invented. Actual bytes still pass the
existing bounded decoder and compressed-size checks.

An optional immutable completion notifier runs on the worker after completion
publication and after releasing the artwork lock. Its context must remain
alive until successful stop. Stop accounts for an in-progress notifier, so
successful stop guarantees no later notification. The notifier can post a UI
event; it must not access UI-owned artwork APIs from the worker thread.
The same notifier can also run synchronously on the UI owner after preparation
newly retires a GPU entry. This schedules the safe follow-up collection/upload
boundary even when no worker completion remains. It must not reenter artwork
APIs. All-pinned pressure emits no recurring notification or polling timer.

## Native host ownership

`AppArtworkHost` is static caller-owned storage. Its staging array is exactly
the existing 768 KiB artwork staging budget, split into 512 KiB decode and
256 KiB upload storage. A bounded arena accounts for this storage. The artwork
object and a dedicated eight-slot worker use the existing shared core arena;
they do not create another core budget. Worker stacks and WinHTTP/WIC internal
allocations remain OS-owned overhead rather than staging bytes.

The host opens only after the first useful present and only when the caller
enables artwork. Opening initializes storage and an idle worker; it performs
no image request or GPU allocation. A completion posts `UI_DIRTY_STATE` to the
existing event queue. A full queue already contains wakeable work; the callback
does not poll, overwrite entries or enqueue owned pointers.

Close disables service access, cancels artwork, stops and joins the worker,
and retires textures before renderer or event-queue destruction. A timeout
keeps all callback storage and dependencies live for another close attempt.
Partial initialization uses the same cleanup path. Core storage is rewound
only when it is still the arena's most recent allocation suffix; otherwise
it retires with the caller's shared arena. Startup-probe and release-benchmark
paths with no artwork should leave the host unopened, keeping their existing
measurements comparable.

The renderer preserves its monotonically increasing texture serial across
destroy/create recovery. Cache lookup/resolve therefore rejects old handles,
including reused slots, and preparation collects their logical cache entries.
The host keeps the same renderer-structure address across recovery. A backend
`UI_RENDER_DEVICE_LOST` during upload is transient capacity pressure:
retain decoded pixels for the surface's scheduled recovery, rather than
negative-caching a content failure. Fallback stops normal app preparation;
host close still retires workers before the queue and renderer storage die.

## Rendering and verification contract

Each texture entry records decoded width and height. The new rounded draw
entry point uses the existing `ui_artwork_draw` centered cover-crop primitive
and caller radius. Missing, failed and stale images render the same bounded
rounded placeholder; the application can separately present an explicit retry
action. Existing `artwork_draw` remains as a radius-zero wrapper.

Deterministic tests use synthetic compressed data, gated fake fetches and a
fake GPU. Cover queued cancellation, late completion, active cancellation,
reappearance, error retention and explicit retry, negative-cache pressure,
generation reset, stale texture handles, crop UVs, corner radii, balanced
resource ownership and no frame allocations. These tests do not imply a live
Spotify account or GPU comparison has run.
