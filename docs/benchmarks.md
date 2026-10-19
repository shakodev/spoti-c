# Release gate (#35)

The gate is implemented but release acceptance is **not complete**. Issue #35
remains open: measured threshold failures and missing live/cold-start evidence
must not become a successful release. The user explicitly requested retaining
the issue while documenting the missing live/cold checks.

## Reproduce

Use the x64 Visual Studio developer shell, the pinned Python environment from
README.md, Windows SDK 10.0.26100.0, CMake and Ninja. Run correctness first:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DENABLE_RELEASE_NATIVE_BENCH=ON
cmake --build build-rel
ctest --test-dir build-rel -R '^(bench_release|bench_idle|bench_navigation)$' --output-on-failure
```

The explicit Release/non-ASan/native opt-in is required. Desktop/GPU benchmarks
are absent from the default correctness suite, and all bench_* registrations
require Release with ENABLE_ASAN=OFF. Use `ctest --test-dir build-rel -N` to
confirm registration; selecting a regex that matches no tests proves nothing.
The import/license audit is a CTest fixture automatically included by the final
gate command. Measurements are serialized. Allow approximately twelve minutes.
Leave the visible application unobscured and avoid mouse/keyboard input during
idle intervals. Input counts are recorded; contaminated intervals fail.

The harness creates a visible instance of the **actual spotic.exe** with its
normal renderer, fonts, SMTC, workers and offline demo playback. Benchmark state
uses a separate `.cache/release-bench.state` under the build working directory;
it does not modify the normal LocalAppData shell state or use account credentials.
The synthetic audio cache is the demo cache under that same build directory.
No working-set trimming, hidden measured windows, subtracted DLL pages, or
relocated application worker processes are used.

## Workloads and outputs

| Runner | Workload / timing | Raw output in build-rel |
|---|---|---|
| bench_idle | Actual app: 3 s warmup + 10 s stopped; load synthetic track; 3 s settle + 3 s warmup + 10 s paused | bench-idle.jsonl |
| bench_release | Process launch to useful frame, 3 s settle; load/play with 3 s settling each; 30 s repeating two-track playback; six 1 s seek intervals; ten bursts of 60 scroll actions spaced 16 ms; one injected renderer loss + 1 s recovery | bench-release.jsonl |
| bench_navigation | 10k on-disk records, 40 source-order filters, initial sort, 40 sorted filters, 10k scrolling Clay layouts | bench-navigation.jsonl, bench-tracklist-samples.jsonl |
| bench_release children | Existing gapless/player/artwork regressions, including bounded artwork cache churn; separate 600 s headless repeat playback, 13 seeks every 45 s | regression exits in bench-release.jsonl; headless-metrics.jsonl |
| bench_dependency_audit | All compiler translation units, pinned vendor hashes/licenses, direct and delay DLL imports, codec/backend configuration, exact artifact hashes/sizes | release-audit.json, *.exe.imports.txt |

GUI process samples are taken externally every 1 s, without messaging the app.
Only interval-boundary snapshots wake the UI; the single ending diagnostic
wake is explicitly accounted for. Zero additional wakes/timeouts/renders/
presents and INFINITE event waits are required in stopped/paused intervals.
An idle-to-error transition also fails. Production input and state changes
continue to render normally.

The headless benchmark includes its local fixture server and a declared 1 Hz
sampler in the measured process. Its fake sink consumes 480 stereo float32
frames per 10 ms; this models scheduling but does not establish WASAPI callback
timeliness. The former 25 ms harness polling was removed. Existing correctness
tests separately check PCM samples, exact gapless duration and ownership.

The artwork child is a deterministic cache/decode/lifetime regression, not a
measurement of real network art churn in the visible app. Similarly, injected
renderer invalidation is not physical GPU removal. These coverage limits remain
open; neither is relabeled live acceptance.

## Counter and threshold definitions

| Quantity | Definition / gate |
|---|---|
| Resident RAM | GetProcessMemoryInfo total and lifetime peak WorkingSetSize, all image/shared/private pages included; GUI peak <20,000,000 and headless peak <8,000,000 bytes |
| Private resident / commit | PROCESS_MEMORY_COUNTERS_EX2 PrivateWorkingSetSize / PrivateUsage; diagnostic values, never subtracted from total RAM |
| CPU | Raw cumulative kernel/user 100 ns units from GetProcessTimes; QPC elapsed time; one-core percent and machine percent divided by active logical processor count |
| Displayed 0.0% | Machine CPU strictly <0.05%; exact values retained. Applies to stopped/paused and sustained playback; active navigation has a separate frame/filter budget |
| Startup | Actual process creation to first useful DX11 present; strictly <30 ms. Current launches have uncontrolled OS file-cache state and cannot certify true cold boot |
| File size | Each application executable strictly <2,000,000 bytes; exact SHA-256 ties import/size records to artifacts |
| GPU | Renderer logical owned bytes and two-buffer swapchain estimate, plus DXGI process local/nonlocal current usage and validity; do not equate logical allocation with driver residency |
| Handles / arenas | OS process handle count each sample, tracked core/audio arena high-water, tracked shutdown balance, plus raw navigation scratch peak |

DXGI usage follows Microsoft's
[QueryVideoMemoryInfo contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiadapter3-queryvideomemoryinfo);
local/nonlocal segment values are reported separately. Frame scratch resets
and static Clay capacity are not claimed as whole-process memory accounting.
The [EX2 process-counter API](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex2)
requires Windows 10/11 22H2 with the September 2023 cumulative update or newer.
Machine name, CPU identifier, Windows build, physical RAM, processor count,
compiler version, GPU PCI IDs/description/driver version and synthetic audio
format are retained in raw records. Loaded image inventory records virtual image
size for diagnosis; it is **not** a resident-memory deduction.

The core gate uses a failure bitmask: 1 resident, 2 executable, 4 cold startup,
8 CPU, 16 idle activity, 32 missing live evidence. Zero/missing counters,
nonfinite timings and invalid CPU sample ordering fail closed. The current
runner deliberately reports cold and live evidence as unverified; it has no
flag that turns a skip or a warm run into acceptance. Completing those workflows
and binding their evidence to the final binaries is remaining #35 work.

## Correctness and evidence

The final default Debug run passes **53/53 ASan CTests**, including strict
threshold boundary/NaN/missing-evidence tests and process-handle balance.
The new delayed-first-request regression proves the loopback fixture listener
survives more than ten idle seconds. It previously stopped accepting before the
paused benchmark loaded a track; the listener now blocks on socket/stop events
without an idle expiration. Request I/O deadlines remain bounded.

REA artifact identity and preimplementation measurement rules are in
[specs/release-gate.md](specs/release-gate.md). This change adds no Spotify API.
[vendor/manifest.json](../vendor/manifest.json) and its pinned license files
remain the dependency source of truth. The audit checks their SHA-256s.
Miniaudio builds only WASAPI and disables unused codecs/engine/backends.
UI shaders compile with fxc at build time; the unused umbrella d3dcompiler link
was removed, and the import audit rejects runtime shader-compiler dependencies.

Published final measurements and logs are in
[verification/release-gate](verification/release-gate/).
The [result summary](verification/release-gate/README.md) records the completed
run: GUI RAM/startup and playing CPU miss their limits; stopped/paused quiescence,
headless RAM, executable size and navigation pass.

## Required external checks, still unverified

- Controlled reboot/cold file-cache launch with machine/boot conditions and
  actual useful-present time. Warm repeat launches are supplementary only.
- Account-backed encrypted playback into physical WASAPI, listener confirmation,
  live Connect transfer/control from a second device, and device-change recovery.
- Whole-client residency while those live services and physical devices run,
  including real artwork churn. The current standalone host still lacks a
  configured live account/collection transport.

Live test registration remains explicitly gated by ENABLE_LIVE_HEADLESS_TEST,
ENABLE_LIVE_CONNECT_TEST and ENABLE_LIVE_AUDIO_TEST. Existing tests document
the required private environment inputs and operator actions. None was asserted
successful in this run. The user chose to defer these checks and keep #35 open.
