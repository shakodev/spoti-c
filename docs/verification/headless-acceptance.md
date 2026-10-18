# Issue #19 acceptance record — 2026-10-18

The required correctness and documented Release resident-memory acceptance
checks pass. Account-backed audible acceptance is explicitly skipped because
private account credentials are unavailable; this is not a compatibility claim.

## Implementation

The user authorized a native CDN transport after a minimal WinHTTP-only probe
exceeded 8 MB before allocating any audio buffers. Authentication, JSON, metadata
and internal APIs remain on WinHTTP. CDN ranges use one bounded Winsock/Schannel
worker in both fixture and live modes. Plain HTTP is explicit loopback-only;
HTTPS uses automatic certificate/hostname validation and chain revocation checks.
The transport rejects credentials, ambiguous framing, unsupported encoding,
redirects and oversized inputs. See [../specs/cdn-transport.md](../specs/cdn-transport.md).

The headless PCM buffer is fixed at 64 KiB; the general sink keeps its 256-KiB
default. Stop/device reopen preserve capacity. Shutdown joins all owners, wipes
every arena's lifetime high-water range plus fixed secret/PCM storage, and
releases the VM allocation. Untouched zero-filled capacity is never populated
merely to wipe it. Working-set trimming and accounting subtraction are absent.

## Correctness

MSVC x64 14.51.36231 (Visual Studio 18 Build Tools), Windows SDK, Ninja,
C17, `/W4 /WX`, Debug `/fsanitize=address`.

The required configure/build and complete default CTest suite pass:
**34/34 tests**, zero compiler warnings or unexpected ASan errors. The five
Release CDN/headless/player/gapless/sink regressions also pass. Public audio
and TLS fixture manifests verify their exact sizes and SHA-256s.

`audio_headless_smoke` compares exact two-track decoded duration and every PCM
sample against an independent libvorbis golden, verifies encrypted cache reuse,
exercises controls, repeats cancellation/shutdown, injects ciphertext corruption,
truncation and bad Content-Range, and checks handle balance over failed startups.
`audio_cdn_transport` checks URL/framing boundaries, exact ciphertext, stale IDs,
in-flight cancellation, repeated resource lifetime and the shared fixed HTTP
arena size. An independent local Python/OpenSSL peer presents a public synthetic
certificate: production Schannel returns `SEC_E_UNTRUSTED_ROOT`, exposes no body
and releases its security handles. Neither process changes Windows trust/key
stores. Python/OpenSSL is a test oracle, not an application dependency.

Explicit `ENABLE_LIVE_CDN_TEST=ON` passes under ASan: native DNS, trusted TLS,
and a complete 49,152-byte HTTPS range across multiple TLS records from httpbin's
public generated-byte endpoint. It uses no account. Explicit
`ENABLE_LIVE_HEADLESS_TEST=ON` compiles and skips with code 77 because private
account credentials are absent. Audible Spotify/WASAPI acceptance remains
**unverified**. Both live options are restored to OFF; live/device/network tests
and the benchmark are absent from default offline ASan CTest.

No C++ translation units, application C++ runtime or external TLS/HTTP DLLs were
added. `dumpbin /dependents` shows C/Windows imports, with WinHTTP, Schannel,
COM, GUI and credential imports deferred until needed.

## Resident measurement

Reproduce using [../headless.md](../headless.md). The fake sink is pinned at
48,000 Hz stereo float32. The nominal 320-kbps synthetic Vorbis fixture averages
362,806 bit/s including container overhead. The local HTTP server and complete
client pipeline share the measured process and start with an empty private cache.

The final Release run completes in **600,015 ms**, renders
**28,799,040 frames**, performs **13 seeks**, reports **zero fake-sink
underruns**, and balances shutdown. It passes at **7,528,448 bytes peak resident
working set**, strictly below **8,000,000 bytes**. The final sample includes
secret wiping, storage release and fixture shutdown. The full raw samples,
OS build, CPU and device identity are in [headless-metrics.jsonl](headless-metrics.jsonl).

Measured benchmark executable SHA-256:
`5db2dd974a56ab2df1534d6857d6345fcf7802ae6fcc939535b6fa68b7b0899e`.
Release CLI executable size: **526,336 bytes**. Rebuilds may produce different
PE hashes; the measurements correspond to this recorded binary.

The read-only `QueryWorkingSetEx` snapshot in
[headless-resident-images.json](headless-resident-images.json) records
**4,853,760 resident image bytes**. This is a diagnostic
subset, not an alternative total-process measurement. The full process peak
includes shared DLL pages, data, stacks, workers, cache and crypto overhead.
ASan residency is never used for RAM acceptance.

This benchmark establishes the documented offline HTTP/fake-device scenario.
It does not measure account startup, Schannel HTTPS, physical WASAPI device
residency/deadlines or audible compatibility. Those remain distinct from the
explicitly permitted skipped account acceptance.
