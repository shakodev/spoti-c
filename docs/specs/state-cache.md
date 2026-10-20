# Offline startup state cache (#34)

Evidence recorded before implementation, 2026-10-19. REA analyzed the exact
`ref/xpui/xpui-snapshot.js` source as inert JavaScript. Parent evidence:
`ev_6dfd3633084e391628e191cac65565223201b0ea79845abc304c922ae7b98ff4`.
The directory digest is
`8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
One 10,948-byte source parsed without failure; 270 files were unselected.
The selected source SHA-256 is
`5e09c3360057a97c56d04ea128034716cdd0eb244ee3951120123f0c0fe3a5d8`.
The loader invokes externally supplied `__webpack_modules__` and names offline
browse/empty-state chunks. This does not recover Spotify's native state format.
REA's `localStorage` forward trace found zero seeds with partial coverage:
`ev_40d9ea050c4235e99fd672aada294f7ad4439f503a13b062cc753d262bf1cb3a`.
No persistence behavior or successful offline launch is inferred from that
negative result. The portable receipts and limits are in state-cache-rea.json.

Source references consulted: librespot `core/src/cache.rs` separates audio,
credentials and volume paths; `core/src/authentication.rs` defines serialized
username/auth_type/auth_data credentials. Those credential fields are explicitly
excluded here. This feature introduces no internal HTTP request or query hash.

## Local format and ownership contract

This is a spoti-c format, not a reconstruction of Spotify's binary cache.
All integers are little endian, encoded field by field; no structs or pointers
are written. A 48-byte header contains magic/version/header size/total size,
CRC32 (over the whole file with checksum bytes zero), cache epoch and checked
offset/count pairs for settings and compact entities. Maximum file: 256 KiB.
Settings hold bounded window geometry, last route, route identity, detail layout,
volume/repeat/shuffle, scroll and last track ID. Up to 128 library entities hold
kind, 128-bit ID and a bounded UTF-8 display label. URLs, tokens, blobs, keys,
account names, artwork and arbitrary serialized strings have no storage fields.
Version 1 is migrated with default repeat/shuffle; version 3 adds the remembered
nonzero app volume at settings offset 76 for persistent mute restoration (#38).
Versions 1/2 derive that preference from their nonzero volume or default to 100%.
Unknown revisions reject. These are local schema definitions, not discovered
historical Spotify formats. Versions 2/3 use 14,992 bytes at their 128-entity maximum, below the 256 KiB
hard limit. The loader rejects larger unsupported layouts before reading them;
it never allocates a full cap-sized buffer from a supplied length.
All failure outcomes reset the output to safe defaults; old epochs retain
preferences but discard identities. IDs are stale display hints only: no cached
identity grants authorization, availability or permission to start playback.

The UI reads once before creating its window and restores an unauthenticated
offline shell. Network/auth/audio/artwork/SMTC setup follows a successful first
present. Saves copy a fixed snapshot to a single owned worker; concurrent submit
returns FULL. The worker writes a unique same-directory temp, flushes, then
atomically replaces the destination. Failed/cancelled writes remove their temp;
an interrupted process can leave an ignored temp, never a partially read final.
Shutdown joins before releasing snapshot storage. No disk or COM work occurs
inside layout/render. Window positions are clamped onto the current monitor.
Startup's three fixed snapshots and timing/worker descriptors are charged to
the existing `MEMORY_BUDGET_STARTUP` reservation with a compile-time size check.
The UI updates its retained snapshot at accepted handoff so navigating away
from Library cannot revert newly captured IDs. Explicit sign-out clears the
borrowed snapshot and the next save drops recent identities. Oversized labels
use a short placeholder; control characters are normalized before persistence.

QPC records process creation (GetProcessTimes correlated with precise system
time), WinMain entry, window, first successful present, and deferred services.
Online-ready remains absent until actually authenticated and online. Release
benchmark launches actual child processes and reports raw milestones plus
p50/p95 for warm-file-cache conditions; it never claims OS-cold measurements
without an operator establishing those conditions. Target misses remain visible.
