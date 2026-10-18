# Random-access Spotify Ogg decryption (#15)

## Evidence recorded before implementation

Inspected 2026-10-18 using the reverse-engineer-anything workflow. REA MCP
`open_binary` selected `C:/Users/Daan/AppData/Roaming/Spotify/Spotify.dll`,
Ghidra provider, native x86-64 PE. REA reported SHA-256
`ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde`.
This identifies the installed artifact; it does not establish execution or
server compatibility. No new HTTP/API endpoint is involved.

REA MCP `search_bytes`, evidence
`ev_d2bf33fb6c5016c00a1bb3f2662ff5631cbbe193a5ab0f86172e5fdc483fd221`,
searched the exact 16-byte IV across 43,737,060 loaded bytes with no matches,
complete within its reported loaded-block coverage, no skipped blocks and no
next cursor. This does not prove audio decryption or the constant is absent:
split/immediate/reordered/derived values are outside that literal query.
It does **not** corroborate the source contract in this binary. The initial
full-analysis startup was stopped; the completed MCP byte-inspection session
used Ghidra 12.1.4 with expensive reference/disassembly analyzers disabled.
No execution or inferred native call graph is claimed. The full returned
Evidence and selected artifact identity are retained in
[decrypt-native-evidence.json](decrypt-native-evidence.json).

The requested pinned reference files establish the implementation contract:

* librespot `19959512c212cbc48a3ff64382fe204f52c659a7`,
  `audio/src/decrypt.rs:5-10,47-57`: AES-128 CTR with a big-endian 128-bit
  counter, IV `72e067fbddcbcf77ebe8bc643f630d93`, and cipher seek to the
  absolute underlying file position.
* `playback/src/player.rs:51,350-385,1120-1142`: only Ogg Vorbis removes
  167 decrypted bytes. Four little-endian IEEE binary32 fields at 144 are
  track gain dB, track peak, album gain dB, album peak, in that order.
* cspot `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`,
  `cspot/src/CDNAudioFile.cpp:85,154`: adds the prefix to logical offsets and
  adds `pos/16` to the IV. Its aligned-only helper and early EOF behavior
  are not adopted for arbitrary byte reads.

These are source observations, consistent with the earlier
[transport evidence](transport-and-playback.md). The reference parser does
not validate the opaque prefix bytes or bound normalization values. No
undocumented prefix magic, checksum, or version is inferred from it.

Reference SHA-256 identities, in the order above:

* `decrypt.rs`: `3248598a3255654c7dceeb5960173e27f8efbf061fc334695691c9e248c5e49fa`
* `player.rs`: `59707163e1ac14f1e7554c4dec3182ed087e409cb95b77bfc1290414a91a879d`
* `CDNAudioFile.cpp`: `c130ed13957155d61bddea16a7e2d5fd7833d86b9eaf44f012052035c550081a`

## Local validation and gain policy

Support metadata formats 0/1/2 (OGG_VORBIS_96/160/320) only; distinguish
unsupported format, truncated prefix, corrupt prefix/normalization, pending
range, stale generation, capacity exhaustion, and crypto failure.
Decrypt bytes [0,172) before mapping: require the full prefix, finite gain
values in [-60,+60] dB, finite peaks in [0,64], and `OggS` plus version zero
at offset 167. Other prefix bytes remain opaque. This is a bounded local
validation policy, not a claim of cryptographic authentication: AES-CTR has
no integrity tag. Ogg page/packet integrity belongs to the decoder.

Normalization is explicit: OFF gives unity; TRACK/ALBUM compute
`10^(gain_db/20)`, capped at unity and at `1/peak` for a nonzero peak.
Zero peak is treated as unknown and prevents amplification via the unity cap.
No implicit pregain, dynamic limiter, or PCM mutation occurs here.

## Ownership and read contract

Logical offset v maps to encrypted offset v+167; reject addition and exclusive
end overflow before acquiring anything. The counter is always derived from
that encrypted offset, including its intra-block remainder. Clamp reads to the
known encrypted size minus 167; a positive read starting at or beyond EOF
returns EOF. A zero read at/before EOF succeeds without output.

Each stream admits one operation. The network owner prepares it by acquiring
a media key lease and immutable range leases for prefix and requested bytes.
On PENDING the owner schedules [0,172) and the mapped payload with the existing
range request API, polls, and retries; the stream itself creates no network
requests or hidden reservations. Acquiring must precede consuming completed
range request status. The range and media handles must describe the same file.

The decoder thread executes `decrypt_read_at` using caller output and reusable
scratch of 172..65536 bytes (at least the clamped read length). It never calls
owner-only media/range mutation APIs. Prefix and payload are decrypted into
scratch, then copied to output only on complete success. Valid scratch is wiped
on every claimed execution exit; encrypted blocks are never modified. CNG's existing
bounded primitive destroys handles and wipes the temporary key schedule after
each use; the stream retains no copied AES key or schedule.

The owner finishes only after decoder completion, with atomic phase publication
providing quiescence, or cancels an operation that has not been dispatched. Do
not dispatch concurrently with cancellation. Finish
releases range and key tickets exactly once. Retirement rejects new operations
and remains pending while an operation is live. Media/range retirement may run
concurrently with the worker, but cannot recycle pinned storage until finish.
Use the same operation address until finish; never copy live stream/operation
objects. No stream storage is reused before retirement completes.

## Verification plan

Independent synthetic Python cryptography AES-CTR fixtures, with public test
key and no Spotify recording/account data, exercise continuous and fragmented
reads, logical offsets 0/1/15/16/17/166/167, cache-block boundaries, and IV byte,
word and dword carry boundaries. Tests also cover format policy, little-endian
float decoding, nonfinite/range failures, truncation, EOF, overflow, unavailable
ranges, source/key generation rejection, cancellation and retirement while a
real decoder worker holds leases. Handle/ticket counts and scratch wiping are
checked in addition to ASan.

Fixtures and SHA-256s are recorded in
[`tests/fixtures/decrypt/manifest.json`](../../tests/fixtures/decrypt/manifest.json).
Regenerate with `tools/decrypt_fixtures.py --write`; run it without arguments
to independently verify the committed binary fixtures. The plaintext is a
documented arithmetic pattern with synthetic prefix fields, not an audio file.
The carry windows cross encrypted offsets 992976 and 51704178384; the continuous
fixture crosses 1744 and both 64 KiB range boundaries.

Debug verification: `/W4 /WX`, `/fsanitize=address`, all 27 default CTest cases
passed, including `audio_ctr_stream` and `audio_spotify_header`. Stream tests
check zero application CRT allocations during reads, CNG handle balance,
one-use range/key tickets, worker handle closure, and zero scratch/key storage
after completion/retirement. Provider failure is injected at every fallible
CNG operation in a cross-block read. Live service compatibility is untested.
