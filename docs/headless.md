# Headless player

Build `spotic-headless` with the normal CMake commands. Every production
translation unit is C17, with the project's strict warnings and Debug ASan.

Offline, deterministic fake output:

```powershell
build/spotic-headless.exe --fixture tests/fixtures/audio/synthetic.enc --cache "$PWD/.cache/headless" --once
```

Omit `--once` for stdin commands: `play`, `pause`, `seek 1500` (milliseconds),
`volume 0.5`, `next`, `stop`, `metrics`, `quit`. CLI `stop` and `quit` join the
owners and wipe the session. `--wasapi` opts into audible fixture playback on
the default Windows device. The fixture HTTP server binds only IPv4 loopback
and reads ciphertext in fixed 4-KiB chunks. The cache contains ciphertext only.
Use a writable private cache directory outside fixtures.

Live playback explicitly opts into the account and physical device:

```powershell
build/spotic-headless.exe --live spotify:track:<base62-id> --credentials <private-DPAPI-file> --country NL --catalogue premium --cache <private-directory> --once
```

The credential file must be an existing `credential_store_save` record with
reusable AP credentials and its matching client ID. This command does not
perform browser login or save account material into fixtures. Playback uses
the existing AP resolver, authenticated AudioKey, token manager, media resolver
and signed CDN range paths. Track IDs, account names, keys, tokens and URLs are
not included in JSON diagnostics.

Correctness:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`audio_headless_smoke` compares every sample with an independent libvorbis
golden, checks two-track exact duration and gapless output, reopens the encrypted
cache, exercises commands, repeats immediate cancellation/shutdown and injects
bad Content-Range, body truncation and corrupt ciphertext. The sink is fake;
these tests do not prove physical device deadlines or audible compatibility.

Build `ENABLE_LIVE_HEADLESS_TEST=ON` to register `audio_headless_live` (otherwise
it is absent). It needs `SPOTIC_HEADLESS_CREDENTIALS`, `SPOTIC_HEADLESS_CACHE`,
`SPOTIC_MEDIA_TRACK`, `SPOTIC_MEDIA_COUNTRY`, `SPOTIC_MEDIA_CATALOGUE`; missing
values return CTest skip 77. It checks five seconds of the device timeline.
Actual listening remains a manual acceptance step. No account is bundled.

Release resident benchmark:

```powershell
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-rel
ctest --test-dir build-rel -R "^(bench_headless)$" --output-on-failure
```

The benchmark is registered only in single-configuration Release, where ASan
is inactive. It runs 600 real seconds, loops two 12-second synthetic tracks at
48 kHz, seeks to 1.5 seconds every 45 seconds (13 seeks), samples once a second
and writes `build-rel/headless-metrics.jsonl`. Its first record pins OS build,
CPU and fake-device format; later records retain raw full-process memory and
audio counters. It uses nominal 320-kbps VBR audio (measured 362,806 bit/s),
starts with an empty private cache, checks >=590 seconds of rendered frames
and zero fake-sink underruns, and fails at peak resident working set >=8,000,000
bytes. The benchmark process includes its local HTTP server. It never trims
the working set. The complete process overhead, including shared Windows DLL
pages, counts. A failure is retained as a failure; application arena usage and
private commit cannot be substituted for residency. An ASan run is never a RAM
acceptance measurement. This fake-device result is separate from live WASAPI.

The final sample includes secret wiping, storage release and fixture shutdown.
Raw samples and the measured executable's hash are recorded in
[verification/headless-acceptance.md](verification/headless-acceptance.md).

Diagnostics use `GetProcessMemoryInfo(PROCESS_MEMORY_COUNTERS_EX2)` for working
set, historical peak, private working set and private commit; `GetProcessHandleCount`
for OS handle count; the existing metrics ledger for aggregate arena high-water;
and atomic callback/wakeup/frame counters. Callback duration is in QPC ticks
with its frequency included. WASAPI callback timings are measured in the device
adapter, using no allocations or locks. Shutdown releases range/key leases,
joins cache jobs and owners, closes HTTP/AP/device handles, verifies accounting,
then securely wipes and releases the fixed VM storage. On an OS teardown timeout,
live storage is retained with explicit failure to avoid freeing a worker's buffers.

MSVC builds defer COM, GUI and credential DLL imports until needed. WASAPI
startup initializes the GUI import on the caller before creating the decode
owner: first-time COM initialization can otherwise give that owner a GUI queue.
Fixture/fake output does not initialize these optional device/credential imports.
