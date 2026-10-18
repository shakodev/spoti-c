# Bounded encrypted audio ranges and disk cache

Issue #14, 2026-10-18. This module consumes resolved `MediaSource` CDN URLs.
It adds no Spotify internal endpoint, GraphQL operation, authentication flow,
decryption, decoder, or audio-device callback. Source resolution and key leases
remain defined in [playback-resolution.md](playback-resolution.md).

## Reference evidence and HTTP policy

Inspected librespot commit `19959512c212cbc48a3ff64382fe204f52c659a7`:
[`audio/src/fetch/mod.rs:103,434`](https://github.com/librespot-org/librespot/blob/19959512c212cbc48a3ff64382fe204f52c659a7/audio/src/fetch/mod.rs#L103)
uses a 64 KiB minimum block, response range verification and disk backing;
[`core/src/spclient.rs:781`](https://github.com/librespot-org/librespot/blob/19959512c212cbc48a3ff64382fe204f52c659a7/core/src/spclient.rs#L781)
constructs inclusive byte ranges without adding account headers to CDN requests.
Also consulted cspot commit `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`,
`cspot/src/CDNAudioFile.cpp`. Its older decoder/decryption buffering is not
the storage policy implemented here. The implementation is original C.

HTTP byte positions and complete lengths follow
[RFC 9110 section 14.4](https://www.rfc-editor.org/rfc/rfc9110.html#section-14.4)
and single-range responses follow
[section 15.3.7](https://www.rfc-editor.org/rfc/rfc9110.html#section-15.3.7).
Implementation admission is deliberately stricter: one exact aligned range,
known total size, identity encoding, and exactly the declared payload length.
No multipart, duplicate Content-Range, overflow, unknown total, conflicting size,
weak/malformed ETag, or changed previously observed ETag is accepted. A valid
short final block ends at `total - 1`. A 200 response means unsupported ranges;
416 establishes EOF only with `bytes */total` and a start at or beyond that
total. Neither response is cached as track data.

The immutable Spotify file ID identifies ciphertext across cache and CDN reads.
Size conflicts poison that source generation. Strong ETags, when supplied, must
remain consistent within the source; disk records also require payload CRC.
This is a file-identity assumption, not a claim that CRC authenticates a server.
No live Spotify/CDN compatibility is claimed by the local fixtures.

Requests use the existing async WinHTTP client, with typed `Range` and
`Accept-Encoding: identity` fields. Decompression is disabled. No cookies,
account headers, custom headers or redirects are used. HTTPS is required except
for an explicit IPv4 loopback test option. Transport/server errors, 408 and 429
can try another URL, with three attempts per block by default (configurable
maximum eight), within the original request deadline (default 8 seconds).
401/403 also expire the failed URL. Exhausted/expired URLs invoke the owner's
source-refresh seam; only the same file ID may replace the URLs. A pending
resolver is driven by its owner's existing event/deadline. Cache hits do not
need a URL or refresh. Ordinary other HTTP errors are terminal.

## Memory and lifetime contract

`RangeFetcher` allocates one fixed metadata object below 64 KiB and exactly
eight 65,536-byte encrypted blocks from separate caller-owned arenas. No
per-request allocation or additional transfer staging is used. There are four
source slots, sixteen request slots and sixteen one-use lease tickets. IDs and
generations never wrap. Cross-block requests must fit the eight-block pool.
An EOF read can return fewer bytes than requested.

Blocks transition EMPTY -> FETCHING -> READY -> PINNED, returning to READY when
their reference count reaches zero. Pending and successful unconsumed requests
reserve their blocks. Acquire immutable spans **before** consuming successful
request status; consuming status releases the request reservation, not a lease.
Readers may use leased bytes on other threads, but all admission, acquire,
release, cancellation and poll calls belong to one network owner thread.
Join/synchronize readers before releasing their lease on that owner thread.
Callback reentry into mutation APIs is rejected.

Unreserved, unpinned blocks with no pending HTTP or disk operation are evicted
by bounded LRU selection. Current requests schedule before next-track prefetch;
a newly admitted current read cancels unrelated active prefetch HTTP. Full
reservations/leases apply backpressure until the caller consumes/releases them.
No growth, buffer overwrite or polling loop is used to bypass backpressure.

Cancellation marks the entire source generation and cancels its I/O. A block
cannot be reused until WinHTTP's final handle-close completion, disk callback
and every lease have retired. Source slots remain occupied while such a block
exists. Late callbacks check both their ticket and source identity before
publishing READY; releasing a copied/stale lease cannot decrement references.

Drive `range_poll`, then wait on `range_wait_handles` with
`range_next_wait_ms` and the owner's other module events/deadlines. Recollect
handles after polling/admission: a full shared worker queue conditionally adds
its idle handle for an eventual retry. Release/admission/status consumption can
request one immediate owner turn; otherwise idle state has no timer. Keep
HTTP/cache/arenas alive through `range_fetcher_shutdown == RANGE_OK`.

## Disk format and worker ownership

`AudioCache` owns one fixed arena object below 256 KiB, including a 4,096-entry
index (no dynamic index growth) and eight jobs. The default 256 MiB file quota
includes all headers and slot padding, allowing 4,092 slots. A smaller configured
quota or entry limit reduces capacity. The file is `encrypted-v1.bin` inside
the caller's absolute cache directory. Version, capacity, quota, file extent,
file ID, aligned offset, complete block length and total size are validated.
The tested x64 build uses 36,688 bytes of range metadata, 230,992 bytes of cache
state (including the 229,376-byte index), and the separate 524,288-byte block
pool. These figures exclude the existing HTTP/worker arenas and OS allocations.

All integers are little-endian; records are serialized explicitly, without
native struct padding:

| Position | Meaning |
| --- | --- |
| File bytes 0..63 | `SPTCAC01`, version 1, block size, capacity, quota, header CRC32 |
| Slot header +0..7 | Complete-record marker `BLK1`, version 1 |
| +8..27, +28..31 | File ID (20 bytes), payload length |
| +32..39, +40..47 | Aligned file offset, total encrypted file size |
| +48..55, +56..63 | Payload CRC32, header CRC32, LRU sequence |
| +64..65599 | Ciphertext, padded to a full 64 KiB slot |

The 64-byte file header is followed by fixed 65,600-byte slots. CRC fields are
zeroed while computing their own header checksum. The complete-record marker
and header checksum are written only after ciphertext and the full slot extent
have been flushed. Invalidation is flushed before overwriting a slot. Damaged
record headers become cache holes; payload corruption invalidates that record.
A truncated/oversized/incompatible file fails initialization explicitly. No
automatic deletion or silent file growth beyond quota occurs. A failed cache
can be bypassed by the range fetcher. Disk failures are exposed through callback
results and statistics, and do not invalidate already verified RAM bytes.
Padding in an evicted slot may retain previous ciphertext and is never returned
as part of a shorter block.

The input is borrowed **ciphertext**, never decoder plaintext. `CacheRecord`
contains no key, URL, token, URI or account metadata. The fetcher copies only
file IDs/URLs from `MediaSource`, not key handles, and passes only file ID,
offset, total and ciphertext to the cache. CRC detects accidental damage, not
malicious changes by a local writer. This is a partial encrypted-block cache,
not a promise that a whole track is present.

Create/open, reads, writes, index scans, CRC, eviction, flush and close execute
on the existing blocking worker. Admission and completion delivery execute on
the network owner. Each admitted get/put receives exactly one callback, including
cancellation. Buffers remain borrowed until that callback. Overlapped I/O uses
the worker's cancellation event and a default 3-second I/O timeout; cancelled
I/O is retired before returning its buffer. Shutdown must finish before stopping
the worker or resetting the cache arena. `CACHE_FULL` never admits a job.

## Deterministic verification

`audio_range_cache` uses loopback HTTP and per-test temporary cache files, with
tracked arena/handle balance and strict-warning ASan compilation. It covers
exact, cross-block, random and EOF reads; rejected 200/malformed 206/416,
truncation and encoding; retry caps and URL expiry/failover; representation
changes; fixed pool/request/lease admission; pinned/full backpressure; current
priority and active prefetch cancellation; offline reopen and cache holes;
file identity/completeness/CRC/version checks; quota eviction and disk-full
injection; shared-worker saturation; cancelled disk/HTTP generations and source
slot reuse; and a reader thread consuming leased bytes during eviction.

Required commands: configure Debug with `ENABLE_ASAN=ON`, build, and run
`ctest --test-dir build --output-on-failure`. The targeted command is
`ctest --test-dir build -R "^(audio_range_cache)$" --output-on-failure`.
