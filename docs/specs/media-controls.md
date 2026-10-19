# Windows media controls (#33)

Recorded before implementation. No Spotify endpoint, persisted query, protocol
command or additional playback state machine is introduced.

## Evidence and boundary

REA parsed the issue-selected ref/xpui/dwp-now-playing-bar.js as inert source:
summary ev_7c723b694e266e707842f6c9bdabdefaf30f21c08a94a25b5d8db3170c7f51cb,
parent ev_3d671b09591983017224afb3da7a5011e21b6e8f7ab3db583e2a0e6165b2db11,
XPUI tree SHA-256 8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728.
One file / 51921 bytes parsed with no failures; 270 files remain unselected.
Semantic coverage is partial with unresolved references; no code was executed.
The retained summary is media-controls-rea.json. The selected module item at
columns 44097..44105 resolves only an external require candidate, not Windows
interop. This evidence does not establish Spotify's SMTC implementation or
duplicate timing policy. ref/librespot/connect/src/spirc.rs was also consulted:
its public play/pause/prev/next methods send commands to its existing owner;
it supplies no Windows COM ABI. Our existing PlayerCommand remains authoritative.

Windows SDK 10.0.26100.0 winrt/windows.media.h and
um/SystemMediaTransportControlsInterop.h provide the authoritative C projections,
method order and IIDs. Use the SDK declarations directly, with short local
aliases and compile-time slot checks. The factory interop IID is
ddb0472d-c911-4a1f-86d9-dc3d71a95f5a; SMTC is
99fa3ff4-1742-42a6-902e-087d41f965ec, and SMTC2 is
ea98d2f6-7f3c-4af2-a586-72889808efb1.

Microsoft documents GetForWindow for a top-level HWND owned by this process:
https://learn.microsoft.com/en-us/windows/win32/api/systemmediatransportcontrolsinterop/nf-systemmediatransportcontrolsinterop-isystemmediatransportcontrolsinterop-getforwindow
Timeline and seek use the SDK TimeSpan ABI (signed 100 ns units):
https://learn.microsoft.com/en-us/uwp/api/windows.media.systemmediatransportcontrols.updatetimelineproperties
https://learn.microsoft.com/en-us/uwp/api/windows.media.systemmediatransportcontrols.playbackpositionchangerequested

## Local ownership and dispatch

One UI owner initializes WinRT, acquires SMTC through GetForWindow and owns its
interfaces, registration tokens, bounded strings and thumbnail reference.
Callbacks may arrive on other threads. They copy into a fixed MPSC queue and
signal the existing app event queue; they never mutate UI/player state or call
decoder/device APIs. Subscription delegates have independently refcounted bounded
setup allocations and a detach gate, so in-flight/late callbacks cannot retain
a pointer to disposed App/control storage. Remove every successful event token,
detach callbacks and release COM before HWND destruction; balance RoInitialize
on its owning thread. Failed initialization unwinds partial registrations.

The UI drains commands using the current immutable PlayerSnapshot. Explicit
Play/Pause are idempotent; old-track/generation events cannot control a reused
track. WM_APPCOMMAND, native media-key messages and SMTC buttons use the same
dispatcher. A local 200 ms cross-source duplicate window pairs matching intents
(including toggle vs explicit play/pause); successful same-source repeats remain
independent. Queue/player FULL is explicit and does not consume a dedup candidate.
Next/Previous duplicates may span the resulting track change; other intents
must still match the track identity. Raw virtual keys and WM_APPCOMMAND have
separate source identities so the three delivery paths can be paired.
This heuristic is our bounded policy, not an observed Spotify value.

## Publication

Publish title/artist/track identity, status and capabilities only on changes.
Thumbnail input is an optional bounded HTTPS URI from already-resolved artwork;
the Windows stream-reference factory owns retrieval and its handle. There are
no invented request headers or duplicate Spotify API calls. Missing metadata
or artwork clears the prior track rather than leaking it into the next one.
System-owned COM/network/driver internals are outside application pool accounting.

Timeline positions come exclusively from latency-corrected audible media frames
in PlayerSnapshot, excluding underrun silence. Use checked frame-to-TimeSpan
conversion and clamp to duration. Publish ordinary position changes at most
once per second, while track/state/seek generation changes publish immediately.
Unknown duration or invalid timeline disables seeking. No new timer, render-loop
allocation, wall-clock playback extrapolation or callback-period UI wake is added.
The existing progress deadline and decode-owner clock acknowledgements drive
publication. Platform absence degrades to existing in-app controls explicitly.

## Physical-device attempt preceding #33

The requested existing ASan headless --wasapi --once path with synthetic.enc
was attempted on this Windows host. WMI reports audio devices, but production
startup failed at decode/device initialization before playback. No successful
physical-device compatibility or audible-output claim follows from that attempt.
The operator-dependent default-device-switch and listener checks remain unverified.

## Native verification findings

The real HWND/WinRT smoke on this host succeeds only after selecting Music on
DisplayUpdater before requesting MusicProperties; the implementation follows
that SDK contract. Title/artist replacement, status and timeline round trips,
event registration/removal and apartment teardown passed under ASan.

Windows' SMTC implementation loads the inbox System32/msvcp_win.dll. No C++
translation unit, C++/WinRT, msvcprt import library or application redistributable
C++ standard runtime is introduced. A literal claim that no C++ DLL exists in
the process cannot be made for this OS service. The native audit reports this
inbox dependency explicitly and rejects other msvcp/concrt/libstdc++ modules;
it does not present the exception as satisfying the issue's literal zero-runtime
wording. See ../verification/media-controls.md for commands and remaining
operator-dependent checks.
