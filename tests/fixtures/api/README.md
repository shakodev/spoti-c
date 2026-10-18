# API evidence fixtures

`manifest.json` identifies every catalog entry, source/Evidence identity, missing
proofs, exact fixture SHA-256/byte length, and collection pointer/count.

Observed HTTP fixtures are local sanitized projections: request methods, header
names/allowlisted values, variable keys/types, response keys, arrays, nulls,
numbers, discriminants and numeric lyric timestamps are preserved. Other strings
(including all lyric words, names, URIs and user/tracking IDs) are replaced before
anything is written or given to REA. Credentials are never stored. These are not
byte-for-byte wire replies; the original response digest and sanitized digest
are different identities. Public service/method names in native RPC fixtures are
preserved; all binary values and personal strings are excluded.

`observed-native-rpc` proves a decoded local IPC shape, never a backing network
endpoint. The observed Search list branch has its own operation and cannot
supply proof for legacy searchDesktop.
`fault-injection` is a controlled test mutation, never an observed Spotify error.
Native RPC and fault-injection classifications supply no missing HTTP proof.

An empty `count_pointer` means no response collection (for example NotFound or a
status-only HTTP error). Counts otherwise name an exact JSON Pointer. The C
checker verifies every fixture's bytes/hash and count; it also requires observed
positive/error/pagination fixtures for every enabled operation. Such checks
protect evidence integrity and do not establish a guessed contract's truth.

The primary Album descriptor is enabled by its observed positive, NotFound and
exhausted-page fixtures and static assembly evidence. Other entries fail closed
while their documented obligations remain incomplete. This is partial issue #21
progress; see `docs/specs/home-library-playlist.md` for the remaining native gap.
