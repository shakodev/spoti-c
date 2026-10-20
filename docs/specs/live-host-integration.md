# Isolated live host integration (#42)

Recorded before importing source on 2026-10-20. The user authorized integrating
the existing uncommitted live adapter from the main worktree into the isolated
issue-42 worktree. Its source base is
`7a1dcb92d502f37f654d8c372400e02a6cd698ba`. The original worktree is read-only
throughout this integration.

The imported [live host specification](live-app.md) retains its existing REA
identities and protocol limitations. [Playback resolution](playback-resolution.md)
retains the recorded provider-status observation. These precede the source
import; this integration invents no endpoint, header, query, URL conversion or
new live compatibility claim. The inherited verification report is historical
provenance and is not evidence that this worktree has passed those checks.

The application adapter consists of `app_live`, account, HTTP, page, Home,
playlist, wire, metadata and lyrics modules. It reuses the existing collection,
catalog, lyrics, credential, HTTP, token, disk-index and headless owners. The
Home/Library image projection is specified independently in
[collection-artwork.md](collection-artwork.md). Its current scope remains
rootlist playlists; personalized Home refusal remains visible.

Required dependency changes are limited to:

- A thread-local background-owner registration, installed only at worker,
  headless network and headless decode entry points. A USER32 queue acquired by
  those explicitly registered service threads must not turn them into the
  actual UI owner. Unregistered UI threads keep the blocking-operation guard.
- Distinct finite token lifetimes: a client-token grant may last up to 31 days;
  Login5 bearer grants retain the existing one-day maximum.
- The already-recorded successful TRACK_V4 provider status of 200, in addition
  to the prior default 0. Other provider statuses remain errors.

The imported live account module is adjusted to use the committed
`HeadlessConfig` and `HeadlessMetrics`. Optional audio-diagnostics fields and
audio-diagnostics file output are removed from this isolated copy. The unrelated
audio callback, range/cache, playback-stall and diagnostic changes in the main
worktree are excluded. No source protocol changes accompany that adjustment.

Default tests remain synthetic or loopback-only: host lifecycle and thread
classification, HTTP retirement/cancellation, wire/model bounds, token expiry
and metadata status handling. Account-dependent acceptance and GPU execution
are separate, explicitly authorized checks; importing these modules does not
execute either. Existing playback support does not authorize a playback test.

Source snapshots for import review (SHA-256):

| Original file | SHA-256 |
| --- | --- |
| `src/ui/app_live.c` | `b62a376466faa8c70769006cb58708158dd877cc6f30ad8a12c53141e35057b1` |
| `src/ui/live_account.c` | `fbf944cb5f69c86eade901383adb7da80b1063988ef3559bfd8d540522fb3406` |
| `docs/specs/live-app.md` | `93cb91a80c1307f4620c23ee6ee1b97fb7863744474ca1c30b3b1593b55656fd` |
