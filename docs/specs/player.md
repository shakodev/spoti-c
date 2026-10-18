# Playback coordinator (#18)

## Evidence recorded before implementation

REA MCP `analyze_javascript_application` inspected the inert installed XPUI
`pip-mini-player-snapshot.js` (265315 bytes, one parsed JavaScript file, no
parse failures). Artifact directory SHA-256:
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Selected file SHA-256 (supplemental byte-identity check):
`9260b144cd1053c23ef75f84fac6d05f7ea38cdb68848986a89cbd4d83e948eb`.
Parent Evidence `ev_4b50e143e986ba6ca0292f661ef27864e16d0cc5e92ef207a64ca644f0b05dc3`;
summary `ev_d8d895bcbcb7f78a390317b4d26280470f4c80c19f3a86aee5692c860d250980`.
The selected graph was capped at 16000 semantic nodes; 270 files were outside
analysis scope. A focused `trace_application_feature` for literal `PlayerAPI`
returned no match in that partial graph (Evidence
`ev_eb00ebf130f9e4eb5759d26f25a8c617b9bb6a77bc5d5300faa9e94fb029e34d`).
This does not establish absence or native playback behavior. No Spotify
process was executed, no requests were captured, and no new endpoint is added.

Source-reference observations, distinct from shipped-artifact REA evidence:
librespot `player.rs:1603` suggests preloading near the end once remaining
bytes are available; `:1984` keeps a gapless sink running; `:2111` reuses a
matching prepared decoder; `:2183` cancels mismatching preloads. Cspot
`TrackQueue.cpp:132..143` cancels pending Mercury/key work on retirement and
`:443` consumes a prepared queue entry. These support the ownership model,
not a promise to reproduce the official client's exact timing or UI policy.

## Implementation policy

One non-GUI decode/device owner drains a fixed MPSC command queue and drives
the existing sink and at most two Vorbis decoders. Network requests travel
through a second bounded MPSC queue; the network owner resolves IDs and
provides generation-tagged logical decrypted source readers. Readers copy
already-prepared data only, return NEED_MORE while network work is pending,
and never invoke network-owner APIs from the decode thread. The existing
MediaResolver/RangeFetcher/DecryptStream retain their owner and lease rules.
Retire messages require network acknowledgement before a reader slot may be
reused; late source-ready messages are rejected. Admission exhaustion returns
FULL without losing commands or retirement obligations.

Current plus one next source/decoder only. Each has a monotonic ticket distinct
from the public playback generation. Seek, replace, stop and format changes
stop/quiesce the sink before clearing PCM and retiring both tickets. New
decoders reopen and seek within the new epoch; no converted old PCM survives.
Paused seek prepares new PCM but keeps the sink stopped until resume.

The queue stores at most 64 16-byte track IDs. Append is bounded; repeat-one
applies to automatic EOF only; repeat-all wraps; explicit next bypasses
repeat-one. Previous restarts after three audible seconds, otherwise moves
back (first entry restarts without wrap unless repeat-all). Shuffle uses a
seeded permutation with the current track fixed; disabling restores insertion
order. A current-source error stops playback; a prefetch error is remembered
and becomes an explicit error at its boundary, without an automatic skip loop.
Queue-clear retains the audible track and removes upcoming IDs. Stop retains
the queue; play restarts its selected track, or the first track after queue end.

Compatible-format gapless handoff inserts a marker at the absolute PCM media
frame boundary, then appends the next decoder's frames into the same running
sink. Public track identity changes when that marker becomes audible, not
when decoding switches. A bounded marker ledger applies backpressure so very
short tracks cannot silently overwrite timeline history.

Timeline maps device output frames through a bounded sink ledger of media spans
and implicit silent gaps, then through track markers. Entirely silent callbacks
do not consume ledger entries, so network stalls do not overflow history.
Raw device position is scaled with
its frequency; if unavailable, submitted output minus padding and negotiated
latency is a conservative fallback. Silence never advances media time. Ledger
overflow invalidates the estimate rather than guessing. Pause freezes the
audible position and restarts its new epoch from there to recover OS-buffered
frames discarded by miniaudio stop/reset. UI timer ticks are never a clock.

All storage is supplied once in fixed arenas (512 KiB codec, 128 KiB decoder
work, 256 KiB PCM plus bounded coordinator metadata in the shared core cap).
No threads, periodic timer, or heap allocations are created by the coordinator.
Callers wait on command/network/source events and the sink producer/control
handles. Network completion producers must quiesce before final destruction.

The embedding network owner maps each RESOLVE ticket to `media_gid_to_uri` /
`media_source_resolve`, opens its `RangeSource` and `DecryptStream`, and publishes
a `PlayerSource` only once the logical file size is known. Its existing #15
prepare/read/finish bridge must arrange encrypted-range reads and lease return;
the player never calls these network-owned APIs from `read_at`. A RETIRE reply
is an acknowledgement that this bridge, its HTTP generations and all leases
are quiescent. This API adds no implicit cross-thread access or account setup.

## Verification

`audio_player` covers virtual device time, latency fallback, long silent gaps,
pause/resume and paused seek, end seeks and invalid positions, replacement
races, stale replies, MPSC command saturation, queue policy, format epochs,
source errors, shutdown acknowledgements, ledger exhaustion and handle counts.
`audio_gapless` uses real #16 decoders and independent golden PCM fixtures:
22.05 kHz mono converted to 48 kHz stereo followed by native 48 kHz stereo.
Every sample across the uninterrupted boundary is checked; no sink restart or
inserted silence is permitted. It also covers cancelled/delayed/failed prefetch.
A CRT allocation hook covers the continuous path after fixture/setup allocation.
Account playback and physical-device acceptance are not claimed by these tests.
