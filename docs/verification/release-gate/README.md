# Issue #35 release measurements — 2026-10-19

**Release acceptance fails. Keep #35 open.** The user deferred live account,
physical-device and controlled cold-boot checks. Reproduction commands and
measurement definitions are in [../../benchmarks.md](../../benchmarks.md).

Machine: AMD Ryzen 5 2600X, 12 logical processors, 17,095,331,840 bytes physical
RAM, Windows 11 build 26200, NVIDIA GeForce RTX 2070 (vendor 4318/device 7943),
driver 32.0.16.1742. MSVC 19.51 / toolset 14.51.36231, x64 C17, SDK 10.0.26100.0,
`/W4 /WX`, Release without ASan. Audio is the synthetic 48 kHz stereo float32
fake sink. The raw identity records preserve machine and QPC information.

## Results

| Gate | Observed | Limit / result |
|---|---:|---|
| GUI peak resident across playback/seek/scroll/injected device loss | 63,299,584 bytes | <20,000,000: **FAIL** |
| Idle/paused app peak resident | 61,980,672 bytes | <20,000,000: **FAIL** |
| Headless peak resident, including fixture server and sampler | 7,581,696 bytes | <8,000,000: pass |
| GUI executable | 989,696 bytes | <2,000,000: pass |
| Headless CLI executable | 528,384 bytes | <2,000,000: pass |
| Useful GUI startup, uncontrolled OS cache, one launch | 305.222 ms | <30 ms: **FAIL**; true cold boot remains unverified |
| Idle and paused CPU | 0.000000000% machine CPU | <0.05%: pass |
| Idle and paused presents/renders/timeouts | 0 during each 10 s interval | pass; no input contamination |
| Idle and paused spontaneous wakes | 0; one explicitly induced ending diagnostic wake each | pass; INFINITE wait |
| GUI playing CPU over 30.2611377 s | 0.107570587% machine / 1.290847044% one core | <0.05% machine: **FAIL** |
| Headless CPU over approximately 600.016 s | 0.071178211% machine / 0.854138530% one core | <0.05% machine: **FAIL** |
| Source-order 10k filter | median 1.9678 ms / nearest-rank p95 2.1044 ms | <16 ms: pass |
| Sorted 10k filter | median 1.9650 ms / nearest-rank p95 2.1271 ms | <16 ms: pass |
| 10k scrolling Clay layouts | 0.06811903 ms/frame, 11 visible rows | <16 ms: pass |
| Fixture player/gapless/artwork regressions | exit 0 each | pass; fixture scope only |
| Live Spotify/WASAPI/Connect, physical device changes, true cold boot | unverified | **BLOCKED** |

The ten-minute headless run rendered **28,793,760 frames**, performed **13 seeks**,
had **zero underruns**, and balanced shutdown. It returns exit 1 for its CPU
threshold even though the independent resident-memory gate passes.
The final GUI run's failure mask is **45**: resident (1), cold/startup (4),
CPU (8), missing live evidence (32). Idle also returns exit 1 for RAM despite
passing quiescence and CPU. These are intentionally failing release gates.

Scroll comprised ten batches of 60 actions with a minimum 16 ms harness wait;
the raw `actions:10` field counts those batches. Actual elapsed scroll time was
15.6822797 s. Six seeks and injected renderer invalidation completed with
healthy playback and zero underruns. The final renderer snapshot remained
ready with 1,441,808 logical owned GPU bytes, a 6,174,464-byte swapchain estimate,
14,757,888 bytes DXGI local process usage and 3,194,880 bytes nonlocal usage.
These quantities are deliberately separate from total process residency.

## Verification and remaining scope

The full default Debug ASan suite passed **53/53** before Release measurement.
It was rerun after strengthening the listener regression to require complete
decoded playback following eleven idle seconds. Final builds use strict
warnings; no compiler warnings or unexpected ASan errors remain.
See [asan-ctest.txt](asan-ctest.txt) and [release-ctest.txt](release-ctest.txt).
The release command has two passes (navigation/audit) and two failures
(release/idle), not an all-green result.

The source/license/import audit checked **368 C translation units**, all pinned
dependency hashes and their license files, and six executable artifacts. It
found no application C++ runtime, ASan runtime or runtime shader compiler in
their direct/delay imports. [release-audit.json](release-audit.json) binds
artifact size and import inventories to SHA-256. Complete dumpbin import
outputs are retained beside it. Runtime loaded-module records include the
Windows/driver images; their virtual image sizes are not deducted from RAM.

The Release cache had ENABLE_RELEASE_NATIVE_BENCH,
ENABLE_RENDERER_GPU_TESTS and ENABLE_STARTUP_NATIVE_BENCH enabled. All account,
network and physical audio opt-ins were OFF. Only the named final gate tests
and their dependency fixture were selected. The baseline Debug options kept
all native/GPU/live opt-ins OFF.

The artwork child checks decode/cache churn and resource lifetime with fake
GPU operations. Real artwork churn in the visible host and its resident cost
remain unmeasured. Injected renderer loss does not prove physical device
recovery. The standalone host also still needs its live account/collection
transport integration. No passing fixture result certifies these gaps.

The GUI probe and headless CLI identities were inspected through REA MCP;
receipts and limitations are linked from
[../../specs/release-gate.md](../../specs/release-gate.md). This change introduces
no new Spotify endpoint.
