# Persistent player volume (#38)

Recorded before implementation, 2026-10-20. This changes this client's UI and
local preferences only. It reuses the existing `PLAYER_VOLUME` command and
`PlayerSnapshot.volume`; no Spotify endpoint, protocol, native artifact or xpui
behavior is inferred or reconstructed.

The existing Settings actions already submit normalized app volume through the
player owner. Connect volume handling also submits that command and publishes
the resulting snapshot. SMTC currently exposes transport/seek controls, not
volume. This change adds no unsupported SMTC or global device-volume mapping.

## State and command ownership

Default app volume and the remembered nonzero level are 100%. The persistent
bar displays a Mute/Unmute button, a continuous slider, and a readable percentage.
Wide layouts share a transport row; compact layouts reserve a second row, with
all controls present and keyboard reachable. Signed-out or unbound playback
shows the retained preference with disabled controls. A stopped but bound player
still accepts app volume. Offline browsing does not disable local playback volume.

Layout only updates bounded widget state and enqueues actions. Preparation
submits commands; no rendering or audio callback allocates or changes volume.
Pointer previews apply continuously, with consecutive queued volume actions
coalesced. Keyboard arrows adjust 1%, Home/End set 0/100%. Losing capture retires
the gesture without reverting already accepted volume commands.

Keep the intended level separately from the last authoritative snapshot while
commands are outstanding. The latest accepted ticket recognizes delayed
and intermediate acknowledgements; unchanged snapshots do not erase a pending
user edit. Add an optional opaque volume acknowledgement ticket to the existing
command/snapshot handoff: `PLAYER_VOLUME.ticket` is copied to the snapshot's
`volume_ticket`. UI tickets increase monotonically; external commands retain
the default zero ticket. Player and UI notification comparisons include the
ticket, so same-level acknowledgements wake once without polling. This
disambiguates rapid mute/unmute returning to the
same value from a stale pre-command snapshot without timers or heuristic
matching of floating-point levels. A new external level supersedes local pending display state and
cancels an active volume gesture. Snapshot reconciliation never submits commands,
so external changes cannot create command feedback loops. Failed/full submissions
retain the previous intended value and report the existing action error state.
Mute remembers the most recent accepted nonzero level; external nonzero changes
refresh that memory too.

## Local persistence

Extend the existing 96-byte settings block without increasing file size: local
schema v3 stores remembered nonzero volume (1..1000) at settings offset 76.
Versions 1/2 migrate the remembered value from their nonzero volume or 100%.
Unknown revisions and out-of-range values still reject. Signed-out shutdown
retains volume preferences while dropping account identities as before.

Restore the intended level before dispatching the first user playback command
once a player binding/session is available. Retry a full/unavailable restore at
the next existing preparation wake, without a new timer or polling loop. Never
replace the saved level with the backend's default snapshot before restoration.

## Verification contract

Cover pointer click/drag/release/cancel, keyboard focus and clamping, mute and
rapid unmute, delayed/intermediate acknowledgements, external mute/nonzero
updates without feedback, bounded backpressure, Settings consistency, navigation,
signed-out/unbound behavior, v1/v2 migration and muted restart restoration.
Retain the zero-heap warm-layout test and the unchanged #35 release gate.
Native visible Release UI checks and optional account/device checks must be
recorded separately from fixture/ASan evidence.

## Release artifact receipt

REA MCP `inspect_pe_resources` recorded the isolated Release executable:
`ev_a3f4291b8f3347a24797446a4719f136a4907c57a9524026d455c97a67dee5bc`,
SHA-256 `d655280557581573a4710e9ff3850578e0e839faddd39924a9fae5bc2c79c0b6`,
999,936 bytes, PE32+ AMD64. The complete
[receipt](player-volume-release-rea.json) establishes static artifact/resource
identity only. Native GUI input and restart observations come from the separate
visible Release acceptance harness, not the static resource inventory.
