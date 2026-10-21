# Sustained playback investigation (#39)

## Scope and reported symptom

Intermittent short audio interruptions occur on **Headphones (TE-C)**, including
idle playback. The initial startup/wakeup fixes did not eliminate the symptom.
The affected device's Windows
mix rate, driver, connection type, track format, cache state and network
conditions have not been captured. Do not infer those from the device name.

The reproducible producer/startup and delayed-fetch defects are addressed below.
Their fixes have not yet passed sustained TE-C listening acceptance. Issue #39
must remain open until correlation and the explicitly opted-in 30-minute
audible Release acceptance are complete. Issue #35's limits remain unchanged.

## Deterministic evidence and fix

The backend contract explicitly permits synchronous rendering inside `start`.
Previously `player_decode_drive` called `sink_start` after the first 1,024-frame
write. A 6,144-frame startup callback therefore returned 1,024 source frames
and 5,120 silent frames despite immediately available source data. The new
`test_playback_stalls.c` regression failed on its zero-underrun assertion before
the production fix. It uses the actual headless capacity: 64 KiB / 8,192 stereo
float frames, with independent libvorbis reference PCM.

The player now primes that existing ring before starting. A completed short
track or track boundary can start with its remaining tail. No buffer is grown.
The same callback drains the primed ring down to exactly low water before the
producer arms its event. Previously the player ignored `sink_producer_arm`'s
false result and could wait until its unrelated timeline timeout. It now
requests another bounded pump if work is runnable. A decoder awaiting input
still waits for `PLAYER_DATA_READY`; EOF still follows the consumed timeline.

The regression compares the entire output and drives refills only on actual
producer wakes. Additional checks deliberately withhold input for three
480-frame device periods, require precisely 1,440 underrun frames, verify a
frozen media clock during silence and compare the exact next sample after
completion. It checks seek correctness and preservation of lifetime diagnostics.
Existing gapless, seek, format-change and callback-race tests remain required.

The encrypted headless integration adds 20 ms to cold-cache HTTP responses and
compares every source sample across two tracks, then repeats from the warm
cache with instrumentation disabled. Its non-realtime fake output pauses when empty, so this
test validates data integrity and timing attribution, **not physical deadlines**.
A separate barrier holds the cache worker for at least 25 ms, verifies that the
queue delay is counted and requires byte-exact cached output after release.

### Delayed block boundaries

The realtime fake sink now consumes a full 480-frame device period every 10 ms,
including while network input is unavailable. Its media counter excludes
inserted silence. The fast non-realtime mode retains its data-integrity role;
both modes still share the decode owner and cannot validate hardware scheduling.

`audio_headless_stalls` uses a fresh encrypted cache and delays each HTTP response
by 150 ms. Before read-ahead, ten seconds of continuous output contained 3,744
underrun frames (78 ms). After the fix, the same test reports zero underruns and
compares all 480,000 stereo frames against the independent reference. It also
performs 40 seeks in one player lifetime to check retirement and bounded request
reuse.

The network owner reserves one following 64 KiB ciphertext block per source,
within the existing eight-block/512 KiB pool. It starts that fetch while the
decoder still has compressed input and queued PCM. Read-ahead advances only as
the decoder reads; it does not download the whole track or enlarge PCM storage.
Cached current reads are resolved before deciding whether to preempt a background
transfer. A regression failed before this change because reading the cached
prefix cancelled useful read-ahead. Seek/shutdown retirement now consumes all
cancelled request handles before reusing the source slot.

### Short process-output capture

A separately authorized process-only WASAPI loopback capture of the initial
`build-rel` executable recorded 29.98 seconds at 44.1 kHz stereo float32. It
contained ten interior near-silent intervals of 2.8–202.3 ms, using a per-channel
threshold of 1e-7 and a minimum interval of 2 ms. Capture reported zero packet
discontinuities and zero nonfinite samples. The early intervals are consistent
with dropouts; without a source comparison or concurrent player diagnostics,
the capture alone does not establish their cause. There was no simultaneous
listening confirmation, and a possible late headphone-volume change makes the
end of the capture less useful. This is not the 30-minute acceptance test.

The recording and capture helpers stay in the ignored `.cache/` directory;
no music recording is checked in. Process-only capture uses Microsoft's
[application loopback API](https://learn.microsoft.com/en-us/samples/microsoft/windows-classic-samples/applicationloopbackaudio-sample/).

## Opt-in diagnostics

`HeadlessConfig.diagnostics` enables a fixed structure of at most 1,024 bytes.
The default is disabled. Each metric has one serialized writer; device loss
uses a separate atomic counter because notifications can arrive concurrently.
Startup checks lock-free integer atomics. The callback records only QPC times
and atomic integers with a fixed number of operations, no allocation, lock,
retry loop, formatting or file I/O. The existing callback allocation guard
remains active. No track IDs, URLs, keys, tokens, names or payload bytes are
included. Storage lives through all worker joins.

Each metric exposes `count`, `total`, `maximum`, `last`, `last_qpc` and
`maximum_qpc`. Time values are raw QPC ticks; divide by `qpc_frequency` for
seconds. Counters persist through seeks, pause/resume and device reopen. The
older top-level `underruns` value is still the current sink epoch only; use
`audio_diagnostics.underrun_frames.total` for lifetime underrun frames.

| Metric | Meaning |
| --- | --- |
| `callback_ticks`, `callback_gap_ticks` | Render duration and running callback start spacing; first callback after start/resume has no gap sample |
| `callback_frames` | Requested device frames, including underrun silence |
| `low_water_frames` | Callback observations at or below low water; value is remaining queued PCM |
| `underrun_frames` | Callback count with missing frames, and missing-frame totals/maxima |
| `producer_wake_ticks` | Signal timestamp to owner resumption for waits returned by the producer event |
| `pump_ticks`, `network_pump_ticks` | Decode/player owner and network-owner processing duration |
| `read_wait_ticks` | Decoder read request to prepared encrypted range; includes scheduling, cache and transport |
| `decrypt_ticks` | Prepared-range decryption on the decode owner |
| `range_ticks` | Admitted HTTP transfer to observed completion, including errors/cancellations |
| `cache_queue_ticks` | Cache job admission to worker execution, including open/close jobs |
| `cache_read_ticks`, `cache_write_ticks` | Disk worker execution, including checksum/flush work and misses/errors |
| `player_state`, `player_command` | Numeric state changes and accepted user commands; internal source replies excluded |
| `track_boundary` | Consumed-timeline automatic track transition generation |
| `source_rate`, `source_channels` | Native decoded source format, once per decoder; includes prefetched sources |
| `invalidations` | Asynchronous device-loss notifications |

Snapshots are race-free individual atomic reads, not a transaction across all
fields. Running snapshots can straddle an update; the joined final snapshot is
stable. Aggregates and last/max timestamps help narrow a cause but are not a
complete event history. Keep an interaction/listening timeline alongside them.
An underrun is recorded even at final drain; correlate it with track completion,
buffering and intentional operations before assigning a cause. Missing samples
are never removed from diagnostic counts to obtain a passing result.

### Offline CLI capture

```powershell
build-rel/spotic-headless.exe --fixture tests/fixtures/audio/synthetic.enc --cache "$PWD/.cache/audio-investigation" --once --diagnostics > build-rel/audio-diagnostics.jsonl
```

This uses the fake 48 kHz stereo device. `--once --diagnostics` writes a sample
approximately once per second and at shutdown from the CLI owner, never from
the audio/network worker. In interactive mode, `metrics` requests a snapshot.
Without `--diagnostics`, the additional timing object is absent. Process QPC,
CPU time, residency and current output format remain available in basic metrics.

For an interval, one-core CPU percentage is
`100 * delta(process_cpu_100ns) / (10,000,000 * delta(sample_qpc)/qpc_frequency)`.
The existing #35 gate reports both this and machine-normalized CPU. Record which
one is being compared; do not substitute private commit for resident memory.

### Later GUI capture (requires separate live opt-in)

Set `SPOTIC_AUDIO_DIAGNOSTICS=1` before launching the Release GUI. On normal
playback-host shutdown/logout, a final snapshot is written to
`%LOCALAPPDATA%/spoti-c/audio-diagnostics.json`. Each session overwrites that one
bounded report; copy it before another session. No logging timer is added to
the UI and no file writes occur on the real-time callback. A crash or forced
termination cannot produce this final report. This setting enables observation,
not automatic playback or a live test.

For live acceptance, record the executable SHA-256, Windows build, TE-C endpoint
and mix format/driver, selected source rate/channels, cold/warm cache condition,
network connection, and timestamped listening observations. Cover 30 minutes of
idle and navigation/scrolling playback, repeated seeks and pause/resume, and
multiple transitions. Preserve both the counters and any audible failures.

## Initial verification record

On 2026-10-19, baseline Debug and all 56 ASan tests passed before this work.
The added startup regression then failed before the fix and passed after it.
Final verification:

- Complete Debug and Release builds passed with MSVC `/W4 /WX`.
- All 56 Debug/ASan CTests passed, including the controlled producer, HTTP and
  disk-delay checks, gapless sample comparisons, seek/loss lifecycle and callback
  allocation/race coverage.
- Release `audio_headless_smoke`, `audio_player`, `audio_gapless`, `audio_sink`,
  `audio_range_cache` and `bench_dependency_audit` all passed (6/6).
- The existing audit checked 385 C compilation units and six artifacts with
  zero failures; no forbidden C++/ASan/shader-compiler imports. `spotic.exe` is
  1,062,400 bytes; `spotic-headless.exe` is 533,504 bytes, both below 2,000,000.
- `git diff --check` passed. The added C modules remain below 500 lines.

Initial Release artifact identities (`build-rel`, before the delayed-fetch fix):

```text
spotic.exe          da2e8b369a93689e64a0b8194340856250d555a42236f3cf7f973cfe32ed61a3
spotic-headless.exe c6bae714934fd06bcb45db95e6f3fc5da102ce7a3288108da3ebf21224541a8c
```

Reproduce a short, separately measured offline CPU/RAM sample with
`python tools/audio_probe.py --build build-rel`. It creates a fresh fixture cache
for each diagnostics-off/on run, measures eight seconds of playback and eight
seconds paused (after one-second settling periods), and writes raw JSONL plus
`build-rel/audio-probe-summary.json`. It validates frame advancement/freeze,
the diagnostic opt-in and balanced teardown. The fake device does not run an
independent hardware deadline; this short sample cannot satisfy #35 or #39.

The [recorded sample](audio-glitches/summary.json) ran on Windows build 26200
with 12 logical processors and a fresh local encrypted cache for each run.
The observer was the separate Python process; reported CPU/RAM belong to the
Release child, including its loopback server. Source and output were synthetic
48 kHz stereo Vorbis/float32. No physical device or external network was used.

| Diagnostics | Playing CPU, one core / machine | Paused CPU | Peak resident bytes |
| --- | --- | --- | --- |
| Off | 0.390611% / 0.032551% | 0.0% | 7,573,504 |
| On | 1.562424% / 0.130202% | 0.0% | 7,557,120 |

The diagnostic-on interval exceeds the existing 0.05% machine CPU gate. This
is retained as a failure against that threshold, not rounded away or used to
relax #35. Eight-second samples and Windows process-time quantization cannot
establish an exact overhead ratio. The full 600-second/GUI gate was not rerun.
Both runs advanced approximately eight seconds of frames while playing and
zero while paused, and completed balanced teardown. The instrumented fake run
reported zero underrun frames and invalidations; this is not audible evidence.
Raw observations: [disabled](audio-glitches/audio-probe-off.jsonl) and
[enabled](audio-glitches/audio-probe-on.jsonl).

Live TE-C acceptance, GUI interaction measurements and universal hardware
compatibility are not established by the offline results.

## Delayed-fetch fix verification

On 2026-10-19 the complete Debug/ASan suite passed **57/57** tests. The six
Release playback checks (`audio_headless_smoke`, `audio_headless_stalls`,
`audio_player`, `audio_gapless`, `audio_sink`, `audio_range_cache`) passed, as did
the separate dependency audit: 382 C compilation units, six artifacts, zero
failures. Debug and Release compiled with `/W4 /WX`; `git diff --check` passed.
Both new regressions failed before their corresponding fixes and passed after.

The updated Release artifacts are in **`build-live`**; the running `build-rel`
executable was left undisturbed. `spotic.exe` is 1,062,912 bytes and
`spotic-headless.exe` is 534,016 bytes. Their SHA-256 identities are:

```text
spotic.exe          185061cd6a533999a4e90bcfa11c8490f9018a93edc61e97e14552b8aedf73a6
spotic-headless.exe 98c409fd66d25b1786e96dac93d33ce4e1a8755bfbbfc061e576290ae29938f5
```

The same short offline probe, now with continuous realtime fake device periods,
produced the [follow-up sample](audio-glitches/read-ahead-summary.json):

| Diagnostics | Playing CPU, one core / machine | Paused CPU | Peak resident bytes |
| --- | --- | --- | --- |
| Off | 0.976519% / 0.081377% | 0.0% | 8,032,256 |
| On | 1.562388% / 0.130199% | 0.0% | 7,540,736 |

Both eight-second playing samples exceed the existing 0.05% machine CPU gate;
the threshold is unchanged. This records the failure rather than establishing
performance acceptance from short, quantized process-time samples. Each run
advanced exactly 384,000 media frames while playing and zero while paused, with
balanced teardown. The instrumented run recorded zero underrun frames. Raw
observations: [disabled](audio-glitches/read-ahead-off.jsonl) and
[enabled](audio-glitches/read-ahead-on.jsonl). The 600-second GUI performance gate
and the updated build's sustained audible acceptance remain pending.
