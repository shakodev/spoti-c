# WASAPI PCM sink (#17)

This module adds no Spotify endpoint or reverse-engineered client behavior.
Source references consulted: `vendor/miniaudio.h` v0.11.25 (shared mode note
at 3639, configuration at 7103, WASAPI state at 7868, event-driven device loop),
and `ref/librespot/playback/src/audio_backend/{mod,rodio}.rs` (sink lifecycle
and predecoded samples only). Rust is reference material, not copied code.

## Contract and scheduling

One non-GUI device-worker thread owns open/start/pause/resume/stop/switch,
clock queries and close. These operations reject other threads. There is no
polling thread. The decoder is the single producer through sink_write; the
WASAPI callback is the sole ring consumer. The caller supplies 256 KiB PCM
storage and keeps the sink, ring and storage alive until close. Stop closes
the producer gate, waits for any in-flight write/arm operation, stops and joins
the device callback, and discards queued PCM. The caller joins its producer
before close or switch; stop wakes its dedicated event so it can exit. Pause preserves
queued PCM; resume retains counters. A fresh start resets counters.

Native float32 mono/stereo only; unsupported native formats fail explicitly.
Rate/channel conversion uses #16 on the decoder worker, never miniaudio's
callback converter. WASAPI shared mode, native rate, no automatic SRC,
variable callback size and low-latency profile retain IAudioClient3 eligibility;
actual eligibility and period remain OS decisions. Automatic stream routing
is disabled. A Windows endpoint notification invalidates the sink and signals
its control event. The worker calls switch after notifying the producer of a
new format epoch. No ring reuse occurs before producer/callback quiescence.

The callback copies whole frames, clamps gain to 0..1 and output to -1..1,
replaces nonfinite input with silence, fills underruns, and updates lock-free
counters. No allocation, lock, decoding, logging or UI signal is used. Its
only event is a decoder low-water wake. The producer resets and arms the
manual-reset event, then rechecks occupancy: a crossing racing with reset
cannot be lost. Each arm allows at most one callback wake, including empty
ring underruns. The producer fills above low water before arming/waiting.

## Memory and clock

Miniaudio allocations use capped, tracked callbacks including allocation
headers and its context/device structures: 128 KiB total, explicit failure
instead of growth. Actual endpoint-buffer bytes are reported separately;
COM/driver internals and OS thread stacks cannot be measured by this ledger.
Allocator/lock audit counters flag attempts from inside our callback.

Clock queries run on the owner, outside the callback. Report raw position,
frequency, correlated QPC in 100 ns units, endpoint padding, stream latency,
submitted frames (including silence), consumed media frames and underruns.
Consumed media frames are ahead of audible playback. Issue #18 adds a bounded
media-span ledger for `played_media_frames`: device position is mapped through
implicit silent gaps, with a padding/latency fallback and explicit validity.
No media position is guessed by subtracting latency across underruns. Clock queries
can fail or be inaccurate; validity is explicit. A clock epoch changes on
start/resume because WASAPI reset may restart raw position. Paused snapshots preserve
the last clock because miniaudio stop can reset its underlying stream.
Pause retains unconsumed ring frames; frames already handed to the OS can be
discarded by that reset. The consumed counter is not a played-frame promise.

Platform contracts: [IAudioClock::GetPosition](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclock-getposition)
(units require frequency; QPC is 100 ns),
[IAudioClock](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nn-audioclient-iaudioclock)
(release on acquiring thread), and
[InitializeSharedAudioStream](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream)
(shared engine periodicity).
Endpoint notification storage stays alive through
[unregistration](https://learn.microsoft.com/en-us/windows/win32/api/mmdeviceapi/nf-mmdeviceapi-immdeviceenumerator-unregisterendpointnotificationcallback)
before device resources and event handles are released.

## Verification

Default tests inject a fake backend and invoke the production callback.
They cover exact frames, wrap, underruns, gain, lifecycle failures, device
loss, clock/latency, producer shutdown and low-water refill races. The optional
ENABLE_LIVE_AUDIO_TEST registers a hardware test; it reports negotiated
rates and waits for an operator default-device change. Skipping that test is
not evidence of physical-device compatibility.
