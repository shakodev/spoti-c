# Issue #19 acceptance record — 2026-10-18

Acceptance is incomplete: the resident-memory benchmark fails the declared
8,000,000-byte threshold. Issue #19 remains open. The application allocation
ledger and private memory are not substitutes for full-process residency.

## Correctness

MSVC x64 14.51.36231 (Visual Studio 18 Build Tools), Windows SDK, Ninja,
C17, `/W4 /WX`, Debug `/fsanitize=address`.

The required configure, build and `ctest --test-dir build --output-on-failure`
passed: **33/33 tests**, zero compiler warnings or unexpected ASan errors.
The four Release headless/player/gapless/sink regressions also passed.
The new smoke test compares exact two-track decoded duration and every PCM
sample against an independent libvorbis golden, verifies encrypted cache reuse,
exercises controls, repeats cancellation/shutdown, injects ciphertext corruption,
truncation and bad Content-Range, and checks OS handle balance across sixteen
failed startup cycles. Additional regressions cover GUI-owner rejection,
deferred device import startup and final shutdown memory counters.

The final ASan CLI fixture `--once` completed with 576,137 frames, zero
underruns and balanced shutdown. Its residency is excluded from RAM acceptance.
The explicit `ENABLE_LIVE_HEADLESS_TEST=ON` target compiled under ASan and
returned skip 77 because private account credentials are unavailable. Audible
account/WASAPI acceptance remains **unverified**. The option was restored to OFF.
Live/device tests and the Release benchmark are absent from default ASan CTest.

No C++ translation units or application C++ runtime imports were added.
`dumpbin /dependents` confirms C/Windows imports and deferred ole32, USER32 and
CRYPT32 imports. WASAPI initializes the GUI import on the caller before creating
the decode owner; a native COM probe confirmed why this initialization order is
necessary. The regression rejects device-mode startup before opening hardware.

## Resident measurement

Reproduce the ten-minute scenario using [../headless.md](../headless.md).
The fake sink is pinned at 48,000 Hz stereo float32. The nominal 320-kbps
synthetic Vorbis fixture averages 362,806 bit/s including container overhead.
The local HTTP server and full client pipeline share the measured process.
No working-set trimming or memory subtraction is performed.

The final Release run completed in **600,016 ms**, rendered
**28,798,560 frames**, performed **13 seeks**, reported **zero fake-sink
underruns**, and balanced shutdown. It failed at **12,062,720 bytes peak resident
working set**, against the strict limit of less than 8,000,000 bytes. The final
sample includes secret wiping, storage release and fixture shutdown.

The complete raw samples, OS build, CPU identity and device format are committed
in [headless-metrics.jsonl](headless-metrics.jsonl). These are measurements of
fake output, separate from physical-device deadlines or audible compatibility.

Measured benchmark executable SHA-256:
`3cae3f8aa9e6f596b50cb81838f22febc2e71bae2b9067adede4db5dec4268dc`.
The Release CLI executable is 512,000 bytes. Rebuilds may produce different PE
hashes; the measurements correspond to this recorded binary.

The read-only `QueryWorkingSetEx` sample in
[headless-resident-images.json](headless-resident-images.json) found
**7,782,400 resident image bytes**. This includes the
executable and loaded Windows/runtime DLLs, not private data or stacks. Deferred
imports avoid eagerly loading unused GUI/device/credential DLLs in fixture mode,
but the full-process peak still exceeds the target. This sample does not
establish an unavoidable lower bound for a redesigned binary or another OS.

Remaining acceptance work: reduce the measured full-process resident peak below
8,000,000 bytes on a documented supported OS/device, then rerun the complete
Release scenario. Live audible acceptance was explicitly skipped, as permitted
when account credentials are unavailable.
